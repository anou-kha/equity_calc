// poker_cli: exact all-in equity from the command line.
//
//   poker_cli AsKs QhQd                     heads-up, preflop
//   poker_cli AsKs QhQd --board Js Ts 2h    on a flop
//   poker_cli AsKs QhQd 7c7d --dead 9c      three players, with a dead card
//
// --board and --dead take every following argument up to the next option, so "Js Ts 2h" and
// "JsTs2h" both work.
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"

namespace {

struct Options {
  std::vector<poker::HoleCards> players;
  std::vector<poker::Card> board;
  std::vector<poker::Card> dead;
};

constexpr std::string_view kUsage =
    "usage: poker_cli <hand> <hand> [<hand> ...] [--board <cards>] [--dead <cards>]\n"
    "  e.g. poker_cli AsKs QhQd --board Js Ts 2h\n";

bool isOption(std::string_view arg) { return arg.starts_with("--"); }

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
    } else if (isOption(arg)) {
      std::cerr << "unknown option: " << arg << '\n' << kUsage;
      return std::nullopt;
    } else if (const auto hole = poker::parseHoleCards(arg)) {
      options.players.push_back(*hole);
    } else {
      std::cerr << "invalid hand (need exactly 2 cards, e.g. AsKs): " << arg << '\n';
      return std::nullopt;
    }
  }
  return options;
}

std::string cardsToString(const std::vector<poker::Card>& cards) {
  std::string text;
  for (const poker::Card card : cards) text += (text.empty() ? "" : " ") + card.toString();
  return text.empty() ? "(none)" : text;
}

std::string percent(double fraction) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << 100.0 * fraction << '%';
  return out.str();
}

void printResult(const Options& options, const poker::EquityResult& result, double seconds) {
  std::cout << "Board: " << cardsToString(options.board) << '\n';
  if (!options.dead.empty()) std::cout << "Dead:  " << cardsToString(options.dead) << '\n';
  std::cout << '\n'
            << std::left << std::setw(8) << "Hand" << std::right << std::setw(10) << "Win"
            << std::setw(10) << "Tie" << std::setw(10) << "Equity" << '\n';
  for (size_t i = 0; i < options.players.size(); ++i) {
    const poker::HoleCards& hole = options.players[i];
    std::cout << std::left << std::setw(8) << hole[0].toString() + hole[1].toString() << std::right
              << std::setw(10) << percent(result.win(i)) << std::setw(10) << percent(result.tie(i))
              << std::setw(10) << percent(result.equity(i)) << '\n';
  }
  std::cout << '\n'
            << result.boards << " boards enumerated in " << std::fixed << std::setprecision(1)
            << seconds * 1000.0 << " ms\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << kUsage;
    return 1;
  }
  const auto options = parseOptions(argc, argv);
  if (!options) return 1;

  const auto start = std::chrono::steady_clock::now();
  const auto outcome = poker::exactEquity(options->players, poker::toMask(options->board),
                                          poker::toMask(options->dead));
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  if (const auto* error = std::get_if<poker::EquityError>(&outcome)) {
    std::cerr << "error: " << poker::toString(*error) << '\n';
    return 1;
  }
  printResult(*options, std::get<poker::EquityResult>(outcome), seconds);
  return 0;
}
