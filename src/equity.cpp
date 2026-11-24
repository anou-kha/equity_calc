// Exact equity by enumeration.
//
// Idea: every card that isn't in a player's hand, on the board, or dead could still be dealt. We
// visit every combination of the missing board cards from that remaining deck exactly once, score
// the resulting showdown, and count wins and ties per player. Because every board is equally
// likely, the counts divided by the number of boards are exact probabilities: no sampling error.
//
// Trade-off: the number of boards grows fast (1.7M heads-up preflop), so this is only practical for
// known hole cards; Monte Carlo (monte_carlo.hpp) handles the cases where enumeration is too slow.
// The inner loop does no heap allocation: the deck and per-player data live in fixed-size arrays.
//
// Multithreading: the boards are split into independent tasks (see exactEquity) that threads take
// one at a time. Every thread counts into its own totals, and the totals are integers, so the
// combined result is identical whatever the number of threads or the order they finish in.
#include "poker/equity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "parallel.hpp"
#include "showdown.hpp"

namespace poker {
namespace {

// Each thread should get at least this many boards: starting a thread costs tens of microseconds,
// about as long as scoring a thousand boards, so tiny jobs (a flop or a turn) stay single-threaded.
constexpr uint64_t kMinBoardsPerThread = 25'000;

// C(n, k): the number of ways to choose k things from n.
uint64_t binomial(uint64_t n, uint64_t k) {
  uint64_t result = 1;
  for (uint64_t i = 1; i <= k; ++i) result = result * (n - k + i) / i;  // always a whole number
  return result;
}

// One thread's counts, in fixed-size arrays so the hot loop never allocates.
struct Totals {
  std::array<PlayerResult, kMaxPlayers> players{};
  uint64_t boards = 0;
};

// A task for the threads: the first few missing cards are fixed (`cards`), and the task enumerates
// every way to choose the rest from deck.cards[next..].
struct Prefix {
  CardMask cards = 0;
  size_t next = 0;
};

// Splits the enumeration into tasks, one for every way of choosing the first `length` of the
// `missing` cards (in deck order, leaving room for the rest). Together they cover every board
// exactly once, like the outer loops of forEachCombination.
void addPrefixes(const detail::Deck& deck, size_t missing, size_t length, size_t first,
                 CardMask cards, std::vector<Prefix>& prefixes) {
  if (length == 0) {
    prefixes.push_back({cards, first});
    return;
  }
  for (size_t i = first; i + missing <= deck.size; ++i) {
    addPrefixes(deck, missing - 1, length - 1, i + 1, cards | deck.cards[i], prefixes);
  }
}

// Calls visit(dealt | chosen) for every way of choosing `count` cards from deck.cards[first..],
// each combination exactly once. Choosing card i and then only cards after it is what stops the
// same set being visited in a different order.
//
// `visit` is a template parameter rather than a std::function, so the compiler can inline it and
// there is no heap allocation or indirect call per board.
template <typename Visit>
void forEachCombination(const detail::Deck& deck, size_t count, size_t first, CardMask dealt,
                        const Visit& visit) {
  if (count == 0) {
    visit(dealt);
    return;
  }
  for (size_t i = first; i + count <= deck.size; ++i) {
    forEachCombination(deck, count - 1, i + 1, dealt | deck.cards[i], visit);
  }
}

double fraction(uint64_t part, uint64_t whole) {
  return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
}

}  // namespace

double EquityResult::win(size_t player) const { return fraction(players[player].wins, boards); }

double EquityResult::tie(size_t player) const { return fraction(players[player].ties, boards); }

double EquityResult::equity(size_t player) const {
  const PlayerResult& p = players[player];
  return fraction(p.wins * kPotUnits + p.tieShares, boards * kPotUnits);
}

std::string_view toString(EquityError error) {
  switch (error) {
    case EquityError::kTooFewPlayers:
      return "at least 2 players are needed";
    case EquityError::kTooManyPlayers:
      return "at most 9 players are supported";
    case EquityError::kInvalidCard:
      return "invalid card";
    case EquityError::kBadBoardSize:
      return "the board must have 0, 3, 4 or 5 cards";
    case EquityError::kDuplicateCard:
      return "the same card appears more than once";
    case EquityError::kNotEnoughCards:
      return "not enough cards left to complete the board";
    case EquityError::kNoTrials:
      return "Monte Carlo needs at least 1 trial";
  }
  return "unknown error";
}

std::variant<EquityResult, EquityError> exactEquity(std::span<const HoleCards> players,
                                                    CardMask board, CardMask dead,
                                                    const ExactOptions& options) {
  if (const auto error = detail::validateDeal(players, board, dead)) return *error;

  const detail::Seats seats = detail::seatPlayers(players);
  const detail::Deck deck = detail::remainingDeck(board | dead | seats.allHoleCards);
  const size_t missing = detail::missingBoardCards(board);
  const size_t numPlayers = players.size();

  // Split the boards into tasks by their first two missing cards (heads-up preflop: 990 tasks).
  // Splitting by just the first card would give only 44 tasks, and the biggest (178,365 boards,
  // over 10% of the work) would leave the other threads waiting at the end; with two cards the
  // biggest task is under 1%. The tasks come out largest first, which also helps keep the threads
  // evenly loaded. A complete board (nothing missing) is a single task.
  const size_t prefixLength = std::min<size_t>(missing, 2);
  std::vector<Prefix> tasks;
  addPrefixes(deck, missing, prefixLength, 0, 0, tasks);

  const uint64_t boards = binomial(deck.size, missing);
  const unsigned threads = detail::threadCount(
      options.threads,
      std::min<uint64_t>(tasks.size(), std::max<uint64_t>(1, boards / kMinBoardsPerThread)));

  const auto perThread =
      detail::runInParallel<Totals>(tasks.size(), threads, [&](uint64_t task, Totals& totals) {
        const std::span<PlayerResult> counts(totals.players.data(), numPlayers);
        const Prefix& prefix = tasks[static_cast<size_t>(task)];
        forEachCombination(deck, missing - prefixLength, prefix.next, board | prefix.cards,
                           [&](CardMask fullBoard) {
                             detail::addToCounts(detail::showdown(fullBoard, seats.holeMasks()),
                                                 counts);
                             ++totals.boards;
                           });
      });

  EquityResult result;
  result.players.resize(numPlayers);
  result.threads = static_cast<unsigned>(perThread.size());
  for (const Totals& totals : perThread) {
    detail::addCounts(std::span(totals.players.data(), numPlayers), result.players);
    result.boards += totals.boards;
  }
  return result;
}

}  // namespace poker
