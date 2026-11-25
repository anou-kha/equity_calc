// poker_cli: all-in equity from the command line, for known hands or ranges.
//
//   poker_cli AsKs QhQd                          heads-up preflop
//   poker_cli AsKs QhQd --board Js Ts 2h         on a flop
//   poker_cli AsKs QhQd 7c7d --dead 9c           three players, with a dead card
//   poker_cli "QQ+, AKs" "JJ-99, AQs"            ranges (notation: see range.hpp)
//   poker_cli AsAh random                        a hand against any two cards
//   poker_cli AsKs QhQd --trials 1000000 --seed 7
//   poker_cli AsKs QhQd --threads 1              single-threaded
//
// Each player is a range; a known hand like AsKs is a range of one. The answer is exact if that
// takes at most 200 million showdowns (about a second on 8 cores), and otherwise a Monte Carlo
// estimate from 10 million random deals, with a 95% margin of error. --exact forces exact
// enumeration; --trials (or --seed) chooses Monte Carlo. Monte Carlo answers are reproducible: the
// same command and seed always print the same numbers.
//
// --board and --dead take every following argument up to the next option, so "Js Ts 2h" and
// "JsTs2h" both work. The work is shared between all hardware threads unless --threads says
// otherwise; the number of threads never changes the answer, only the time.
#include <algorithm>
#include <charconv>
#include <chrono>
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
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/range.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kDefaultSeed = 1;
constexpr uint64_t kDefaultTrials = 10'000'000;
constexpr uint64_t kExactLimit = 200'000'000;  // most showdowns to enumerate without --exact
constexpr uint64_t kMaxThreads = 256;          // the library never uses more than this

struct Options {
  std::vector<std::string> rangeTexts;  // each player's range as typed
  std::vector<poker::Range> players;
  std::vector<poker::Card> board;
  std::vector<poker::Card> dead;
  bool exact = false;              // --exact
  std::optional<uint64_t> trials;  // --trials: Monte Carlo
  std::optional<uint64_t> seed;    // --seed: Monte Carlo
  unsigned threads = 0;            // 0: one per hardware thread
};

constexpr std::string_view kUsage =
    "usage: poker_cli <range> <range> [<range> ...] [--board <cards>] [--dead <cards>]\n"
    "                 [--exact | --trials <n> [--seed <n>]] [--threads <n>]\n"
    "  e.g. poker_cli AsKs QhQd --board Js Ts 2h\n"
    "       poker_cli \"QQ+, AKs\" random\n";

bool isOption(std::string_view arg) { return arg.starts_with("--"); }

// Parses a whole number written with digits only, e.g. "1000000".
std::optional<uint64_t> parseNumber(std::string_view text) {
  uint64_t value = 0;
  const char* end = text.data() + text.size();
  const auto [stop, error] = std::from_chars(text.data(), end, value);
  if (error != std::errc{} || stop != end) return std::nullopt;  // also rejects "", "-1", "1e6"
  return value;
}

// Parses the arguments, or prints what is wrong and returns std::nullopt.
std::optional<Options> parseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--board" || arg == "--dead") {
      std::string text;  // every argument up to the next option, space-separated
      while (i + 1 < argc && !isOption(argv[i + 1])) {
        if (!text.empty()) text += ' ';
        text += argv[++i];
      }
      const auto cards = poker::parseCards(text);
      if (!cards) {
        std::cerr << "invalid cards after " << arg << ": " << text << '\n';
        return std::nullopt;
      }
      (arg == "--board" ? options.board : options.dead) = *cards;
    } else if (arg == "--exact") {
      options.exact = true;
    } else if (arg == "--trials" || arg == "--seed" || arg == "--threads") {
      std::optional<uint64_t> value;
      if (i + 1 < argc) value = parseNumber(argv[++i]);
      if (!value) {
        std::cerr << arg << " needs a whole number, e.g. " << arg << ' '
                  << (arg == "--threads" ? "8" : "1000000") << '\n';
        return std::nullopt;
      }
      if (arg == "--trials") {
        options.trials = *value;
      } else if (arg == "--seed") {
        options.seed = *value;
      } else if (*value > kMaxThreads) {
        std::cerr << "--threads must be at most " << kMaxThreads << " (0 means all of them)\n";
        return std::nullopt;
      } else {
        options.threads = static_cast<unsigned>(*value);
      }
    } else if (isOption(arg)) {
      std::cerr << "unknown option: " << arg << '\n' << kUsage;
      return std::nullopt;
    } else {
      const auto parsed = poker::parseRange(arg);
      if (const auto* error = std::get_if<poker::RangeError>(&parsed)) {
        std::cerr << "invalid range \"" << arg << "\"";
        if (error->token != arg) std::cerr << " at \"" << error->token << "\"";
        std::cerr << ": " << error->reason << '\n';
        return std::nullopt;
      }
      options.rangeTexts.emplace_back(arg);
      options.players.push_back(std::get<poker::Range>(parsed));
    }
  }
  if (options.exact && (options.trials || options.seed)) {
    std::cerr << "--exact can't be combined with --trials or --seed\n";
    return std::nullopt;
  }
  return options;
}

std::string cardsToString(const std::vector<poker::Card>& cards) {
  std::string text;
  for (const poker::Card card : cards) text += (text.empty() ? "" : " ") + card.toString();
  return text.empty() ? "(none)" : text;
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

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool allKnownHands(const Options& options) {
  return std::all_of(options.players.begin(), options.players.end(),
                     [](const poker::Range& range) { return range.size() == 1; });
}

// Prints the error if `outcome` holds one, and returns whether it did.
template <typename Result>
bool printIfError(const std::variant<Result, poker::EquityError>& outcome) {
  if (const auto* error = std::get_if<poker::EquityError>(&outcome)) {
    std::cerr << "error: " << poker::toString(*error) << '\n';
    return true;
  }
  return false;
}

// One row per player. `margins` holds each player's 95% margin of error for Monte Carlo results
// and is empty for exact ones.
void printTable(const Options& options, const poker::EquityResult& result,
                const std::vector<double>& margins) {
  std::cout << "Board: " << cardsToString(options.board) << '\n';
  if (!options.dead.empty()) std::cout << "Dead:  " << cardsToString(options.dead) << '\n';

  size_t rangeWidth = std::string_view("Range").size();
  for (const std::string& text : options.rangeTexts) rangeWidth = std::max(rangeWidth, text.size());
  const int firstColumn = static_cast<int>(rangeWidth) + 2;

  std::cout << '\n'
            << std::left << std::setw(firstColumn) << "Range" << std::right << std::setw(6)
            << "Hands" << std::setw(10) << "Win" << std::setw(10) << "Tie" << std::setw(10)
            << "Equity";
  if (!margins.empty()) std::cout << std::setw(14) << "95% margin";
  std::cout << '\n';
  for (size_t i = 0; i < options.players.size(); ++i) {
    std::cout << std::left << std::setw(firstColumn) << options.rangeTexts[i] << std::right
              << std::setw(6) << options.players[i].size() << std::setw(10)
              << percent(result.win(i)) << std::setw(10) << percent(result.tie(i)) << std::setw(10)
              << percent(result.equity(i));
    if (!margins.empty()) std::cout << std::setw(14) << "+/- " + percent(margins[i]);
    std::cout << '\n';
  }
  std::cout << '\n';
}

int runExact(const Options& options) {
  const auto start = Clock::now();
  const auto outcome =
      poker::exactEquity(options.players, poker::toMask(options.board), poker::toMask(options.dead),
                         {.threads = options.threads});
  const double seconds = secondsSince(start);
  if (printIfError(outcome)) return 1;

  const auto& result = std::get<poker::EquityResult>(outcome);
  printTable(options, result, {});
  if (result.deals == 1) {
    std::cout << withCommas(result.boards) << " boards";
  } else {
    std::cout << withCommas(result.boards) << " showdowns (" << withCommas(result.deals)
              << " deals of the hands x " << withCommas(result.boards / result.deals) << " boards)";
  }
  std::cout << " enumerated in " << milliseconds(seconds) << onThreads(result.threads) << '\n';
  return 0;
}

// `automatic`: Monte Carlo was chosen because exact enumeration would take too long.
int runMonteCarlo(const Options& options, bool automatic) {
  const poker::MonteCarloOptions monteCarlo = {.trials = options.trials.value_or(kDefaultTrials),
                                               .seed = options.seed.value_or(kDefaultSeed),
                                               .threads = options.threads};
  const auto start = Clock::now();
  const auto outcome = poker::monteCarloEquity(options.players, poker::toMask(options.board),
                                               poker::toMask(options.dead), monteCarlo);
  const double seconds = secondsSince(start);
  if (printIfError(outcome)) return 1;

  const auto& result = std::get<poker::MonteCarloResult>(outcome);
  std::vector<double> margins;
  for (size_t i = 0; i < options.players.size(); ++i) {
    margins.push_back(result.equity(i).margin95());
  }
  printTable(options, result.counts, margins);
  std::cout << "Monte Carlo: " << withCommas(monteCarlo.trials) << " random "
            << (allKnownHands(options) ? "boards" : "deals") << " (seed " << monteCarlo.seed
            << ") in " << milliseconds(seconds) << onThreads(result.counts.threads) << '\n';
  if (automatic) {
    std::cout << "Exact enumeration would take over " << withCommas(kExactLimit)
              << " showdowns; --exact forces it.\n";
  }
  std::cout << "The true equity is within the margin for 95% of seeds.\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << kUsage;
    return 1;
  }
  const auto options = parseOptions(argc, argv);
  if (!options) return 1;
  // Build the evaluator's lookup tables (a few ms, once) before any timing starts, so the reported
  // time measures only the equity calculation.
  poker::buildEvaluatorTables();

  if (options->trials || options->seed) return runMonteCarlo(*options, false);
  if (options->exact) return runExact(*options);
  const uint64_t showdowns = poker::exactShowdowns(options->players, poker::toMask(options->board),
                                                   poker::toMask(options->dead), kExactLimit);
  return showdowns <= kExactLimit ? runExact(*options) : runMonteCarlo(*options, true);
}
