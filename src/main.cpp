// poker_cli: all-in equity from the command line, for known hands or ranges. `poker_cli --help`
// describes everything it can do. The work itself is done by the calculator layer
// (calculator.hpp), which the WebAssembly build shares, so this file only reads the arguments and
// prints the report.
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include "poker/calculator.hpp"

namespace {

constexpr uint64_t kMaxThreads = 256;  // the library never uses more than this

constexpr std::string_view kUsage =
    "usage: poker_cli <range> <range> [<range> ...] [--board <cards>] [--dead <cards>]\n"
    "                 [--exact | --trials <n> [--seed <n>]] [--threads <n>] [--json]\n"
    "Try 'poker_cli --help' for the range notation, all options and examples.\n";

constexpr std::string_view kHelp =
    R"(poker_cli: Texas Hold'em all-in equity, exact or by Monte Carlo simulation.

Usage: poker_cli <range> <range> [<range> ...] [options]

Each player is a range of possible hands; a known hand like AsKs is a range of one.
Combine entries with commas, and quote a range that contains spaces ("QQ+, AKs").
  QQ            a pair                                   6 hands
  AKs AKo AK    suited / offsuit / both                  4 / 12 / 16 hands
  QQ+           QQ, KK, AA                               18 hands
  ATs+          ATs, AJs, AQs, AKs                       16 hands
  JJ-99         every pair from JJ down to 99            18 hands
  A5s-A2s       A5s, A4s, A3s, A2s                       16 hands
  AhKh          one specific hand                        1 hand
  random, any   any two cards                            1,326 hands

Options:
  --board <cards>   0, 3, 4 or 5 board cards, e.g. --board Js Ts 2h (spaces optional)
  --dead <cards>    cards nobody can be dealt, e.g. folded or exposed cards
  --exact           enumerate every deal and board, however long that takes
  --trials <n>      Monte Carlo with n random deals (default 10,000,000)
  --seed <n>        the Monte Carlo random stream (default 1); implies Monte Carlo
  --threads <n>     threads to use (default 0: all hardware threads)
  --json            print the result as JSON
  --help            show this help
  --version         show the version

Without --exact, --trials or --seed, the answer is exact when that takes at most 200,000,000
showdowns, and otherwise a Monte Carlo estimate with a 95% margin of error. The same command and
seed always print the same numbers, whatever the number of threads. Exact enumeration of ranges
handles up to 4,194,304 deals of the hands (one hand from each range).

Examples:
  poker_cli AsKs QhQd --board Js Ts 2h
  poker_cli AA KK
  poker_cli "QQ+, AKs" random
  poker_cli AsKs QhQd --trials 1000000 --seed 7 --json

Exit status: 0 on success, 1 if the input is invalid.
)";

struct Options {
  poker::CalculationRequest request;
  bool json = false;
  bool help = false;
  bool version = false;
};

bool isOption(std::string_view arg) { return arg.starts_with("--"); }

// Parses a whole number written with digits only, e.g. "1000000".
std::optional<uint64_t> parseNumber(std::string_view text) {
  uint64_t value = 0;
  const char* end = text.data() + text.size();
  const auto [stop, error] = std::from_chars(text.data(), end, value);
  if (error != std::errc{} || stop != end) return std::nullopt;  // also rejects "", "-1", "1e6"
  return value;
}

// Parses the arguments, or prints what is wrong and returns std::nullopt. Ranges and cards are
// only collected here; calculate() checks them, so the CLI and the web build report the same
// errors.
std::optional<Options> parseOptions(int argc, char** argv) {
  Options options;
  poker::CalculationRequest& request = options.request;
  request.threads = 0;  // the CLI uses every hardware thread unless told otherwise
  bool exact = false;
  std::optional<uint64_t> trials;
  std::optional<uint64_t> seed;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else if (arg == "--version") {
      options.version = true;
    } else if (arg == "--json") {
      options.json = true;
    } else if (arg == "--exact") {
      exact = true;
    } else if (arg == "--board" || arg == "--dead") {
      std::string text;  // every argument up to the next option, space-separated
      while (i + 1 < argc && !isOption(argv[i + 1])) {
        if (!text.empty()) text += ' ';
        text += argv[++i];
      }
      (arg == "--board" ? request.board : request.dead) = text;
    } else if (arg == "--trials" || arg == "--seed" || arg == "--threads") {
      std::optional<uint64_t> value;
      if (i + 1 < argc) value = parseNumber(argv[++i]);
      if (!value) {
        std::cerr << arg << " needs a whole number, e.g. " << arg << ' '
                  << (arg == "--threads" ? "8" : "1000000") << '\n';
        return std::nullopt;
      }
      if (arg == "--trials") {
        trials = *value;
      } else if (arg == "--seed") {
        seed = *value;
      } else if (*value > kMaxThreads) {
        std::cerr << "--threads must be at most " << kMaxThreads << " (0 means all of them)\n";
        return std::nullopt;
      } else {
        request.threads = static_cast<unsigned>(*value);
      }
    } else if (isOption(arg)) {
      std::cerr << "unknown option: " << arg << '\n' << kUsage;
      return std::nullopt;
    } else {
      request.ranges.emplace_back(arg);
    }
  }

  if (exact && (trials || seed)) {
    std::cerr << "--exact can't be combined with --trials or --seed\n";
    return std::nullopt;
  }
  if (exact) {
    request.method = poker::Method::kExact;
  } else if (trials || seed) {
    request.method = poker::Method::kMonteCarlo;
  }
  if (trials) request.trials = *trials;
  if (seed) request.seed = *seed;
  return options;
}

// "61,642,944"
std::string withCommas(uint64_t number) {
  std::string digits = std::to_string(number);
  for (size_t i = digits.size(); i > 3; i -= 3) digits.insert(i - 3, ",");
  return digits;
}

// "69.0 ms", or "0.041 ms" for times under a millisecond, which would otherwise print as "0.0".
std::string milliseconds(double seconds) {
  const double ms = seconds * 1000.0;
  std::ostringstream out;
  out << std::fixed << std::setprecision(ms < 1.0 ? 3 : 1) << ms << " ms";
  return out.str();
}

std::string percent(double fraction) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << 100.0 * fraction << '%';
  return out.str();
}

// " on 1 thread" / " on 16 threads"
std::string onThreads(unsigned threads) {
  return " on " + std::to_string(threads) + (threads == 1 ? " thread" : " threads");
}

void printReport(const poker::CalculationReport& report) {
  const bool monteCarlo = report.method == poker::Method::kMonteCarlo;
  std::cout << "Board: " << (report.board.empty() ? "(none)" : report.board) << '\n';
  if (!report.dead.empty()) std::cout << "Dead:  " << report.dead << '\n';

  // One row per player.
  size_t rangeWidth = std::string_view("Range").size();
  for (const poker::PlayerReport& player : report.players) {
    rangeWidth = std::max(rangeWidth, player.range.size());
  }
  const int firstColumn = static_cast<int>(rangeWidth) + 2;
  std::cout << '\n'
            << std::left << std::setw(firstColumn) << "Range" << std::right << std::setw(6)
            << "Hands" << std::setw(10) << "Win" << std::setw(10) << "Tie" << std::setw(10)
            << "Equity";
  if (monteCarlo) std::cout << std::setw(14) << "95% margin";
  std::cout << '\n';
  for (const poker::PlayerReport& player : report.players) {
    std::cout << std::left << std::setw(firstColumn) << player.range << std::right << std::setw(6)
              << player.hands << std::setw(10) << percent(player.win) << std::setw(10)
              << percent(player.tie) << std::setw(10) << percent(player.equity);
    if (monteCarlo) std::cout << std::setw(14) << "+/- " + percent(player.margin95);
    std::cout << '\n';
  }
  std::cout << '\n';

  // How the answer was found.
  if (!monteCarlo) {
    if (report.deals == 1) {
      std::cout << withCommas(report.showdowns) << " boards";
    } else {
      std::cout << withCommas(report.showdowns) << " showdowns (" << withCommas(report.deals)
                << " deals of the hands x " << withCommas(report.showdowns / report.deals)
                << " boards)";
    }
    std::cout << " enumerated in " << milliseconds(report.seconds) << onThreads(report.threads)
              << '\n';
    return;
  }
  std::cout << "Monte Carlo: " << withCommas(report.showdowns) << " random "
            << (report.knownHands ? "boards" : "deals") << " (seed " << report.seed << ") in "
            << milliseconds(report.seconds) << onThreads(report.threads) << '\n';
  if (report.automatic) {
    std::cout << "Exact enumeration would take over " << withCommas(poker::kAutoExactLimit)
              << " showdowns; --exact forces it.\n";
  }
  std::cout << "The true equity is within the margin for 95% of seeds.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << kUsage;
    return 1;
  }
  const auto options = parseOptions(argc, argv);
  if (!options) return 1;
  if (options->help) {
    std::cout << kHelp;
    return 0;
  }
  if (options->version) {
    std::cout << "poker_cli " << poker::version() << '\n';
    return 0;
  }

  const auto outcome = poker::calculate(options->request);
  if (const auto* error = std::get_if<poker::CalculationError>(&outcome)) {
    if (options->json) {
      std::cout << poker::toJson(*error) << '\n';
    } else {
      std::cerr << "error: " << error->message << '\n';
    }
    return 1;
  }
  const auto& report = std::get<poker::CalculationReport>(outcome);
  if (options->json) {
    std::cout << poker::toJson(report) << '\n';
  } else {
    printReport(report);
  }
  return 0;
}
