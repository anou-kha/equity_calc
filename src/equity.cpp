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
#include <limits>
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

// Part of a task for the threads: the deck positions of the first few missing cards. The task
// enumerates every way to choose the rest from positions next, next + 1, ... Positions rather than
// cards, because each deal leaves a different deck (but always one of the same size).
struct Prefix {
  std::array<size_t, 2> positions{};
  size_t length = 0;
  size_t next = 0;
};

// One prefix for every way of choosing the first `length` of the `missing` cards from a deck of
// `deckSize` cards (in deck order, leaving room for the rest). Together they cover every board
// exactly once, like the outer loops of forEachCombination.
void addPrefixes(size_t deckSize, size_t missing, size_t length, size_t first, Prefix prefix,
                 std::vector<Prefix>& prefixes) {
  if (prefix.length == length) {
    prefix.next = first;
    prefixes.push_back(prefix);
    return;
  }
  for (size_t i = first; i + (missing - prefix.length) <= deckSize; ++i) {
    Prefix longer = prefix;
    longer.positions[longer.length++] = i;
    addPrefixes(deckSize, missing, length, i + 1, longer, prefixes);
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
    case EquityError::kEmptyRange:
      return "a range has no hands left once the board and dead cards are taken out";
    case EquityError::kNoValidDeal:
      return "the ranges can't all be dealt at once (they need the same cards)";
    case EquityError::kTooManyDeals:
      return "too many deals of the hands to enumerate exactly (over 4,194,304); use Monte Carlo";
  }
  return "unknown error";
}

std::variant<EquityResult, EquityError> exactEquity(std::span<const HoleCards> players,
                                                    CardMask board, CardMask dead,
                                                    const ExactOptions& options) {
  // Validate known hands with their own, more specific errors (e.g. kDuplicateCard rather than
  // kNoValidDeal), then treat each hand as a range of one.
  if (const auto error = detail::validateDeal(players, board, dead)) return *error;
  return exactEquity(detail::singleHandRanges(players), board, dead, options);
}

std::variant<EquityResult, EquityError> exactEquity(std::span<const Range> players, CardMask board,
                                                    CardMask dead, const ExactOptions& options) {
  const auto prepared = detail::liveHands(players, board, dead);
  if (const auto* error = std::get_if<EquityError>(&prepared)) return *error;
  const auto& hands = std::get<detail::LiveHands>(prepared);

  // The deals are listed before any board is enumerated, so that threads can take (deal, prefix)
  // tasks by number. Counting them first (stopping just past the limit) keeps a spot with billions
  // of deals from trying to list them all and running out of memory.
  uint64_t numDeals = 0;
  detail::forEachDeal(hands, [&](const detail::Seats&) { return ++numDeals <= kMaxExactDeals; });
  if (numDeals == 0) return detail::noValidDealError(players);
  if (numDeals > kMaxExactDeals) return EquityError::kTooManyDeals;

  std::vector<detail::Seats> deals;
  deals.reserve(static_cast<size_t>(numDeals));
  detail::forEachDeal(hands, [&](const detail::Seats& seats) {
    deals.push_back(seats);
    return true;
  });

  const size_t numPlayers = players.size();
  const size_t missing = detail::missingBoardCards(board);
  const auto deckSize = static_cast<size_t>(detail::cardsLeftAfterDeal(board, dead, numPlayers));

  // Tasks are (deal, prefix) pairs, where a prefix fixes the first few missing cards.
  //  - Preflop, a deal has 1,712,304 boards heads-up, so it's split by the first two missing cards
  //    (990 prefixes). Splitting by just the first would give 44 tasks for a known hand, and the
  //    biggest (178,365 boards, over 10% of the work) would leave the other threads waiting at the
  //    end; with two cards the biggest task is under 1%.
  //  - After the flop, a deal has at most 990 boards, so each deal is one task (an empty prefix).
  //    Splitting it further would make most tasks a single board, each rebuilding the deck first,
  //    which took longer than scoring the board. The threads still finish together: each gets at
  //    least 25,000 boards (kMinBoardsPerThread), so at least 25 equally sized deals.
  const size_t prefixLength = board == 0 ? 2 : 0;
  std::vector<Prefix> prefixes;
  addPrefixes(deckSize, missing, prefixLength, 0, Prefix{}, prefixes);
  const uint64_t numTasks = deals.size() * prefixes.size();

  const uint64_t boards = deals.size() * binomial(deckSize, missing);
  const unsigned threads = detail::threadCount(
      options.threads,
      std::min<uint64_t>(numTasks, std::max<uint64_t>(1, boards / kMinBoardsPerThread)));

  const auto perThread =
      detail::runInParallel<Totals>(numTasks, threads, [&](uint64_t task, Totals& totals) {
        const detail::Seats& deal = deals[static_cast<size_t>(task / prefixes.size())];
        const Prefix& prefix = prefixes[static_cast<size_t>(task % prefixes.size())];
        const detail::Deck deck = detail::remainingDeck(board | dead | deal.allHoleCards);
        CardMask prefixCards = 0;
        for (size_t i = 0; i < prefix.length; ++i) prefixCards |= deck.cards[prefix.positions[i]];

        // Choose the scorer once per task, not once per board, so each gets its own loop (as in
        // monte_carlo.cpp, where a per-board test slowed the 3+ player loop down).
        const std::span<PlayerResult> counts(totals.players.data(), numPlayers);
        const CardMask start = board | prefixCards;
        const size_t toDeal = missing - prefixLength;
        if (numPlayers == 2) {
          forEachCombination(deck, toDeal, prefix.next, start, [&](CardMask fullBoard) {
            detail::addHeadsUpShowdown(fullBoard, deal, counts.first<2>());
            ++totals.boards;
          });
        } else {
          forEachCombination(deck, toDeal, prefix.next, start, [&](CardMask fullBoard) {
            detail::addToCounts(detail::showdown(fullBoard, deal.holeMasks()), counts);
            ++totals.boards;
          });
        }
      });

  EquityResult result;
  result.players.resize(numPlayers);
  result.deals = deals.size();
  result.threads = static_cast<unsigned>(perThread.size());
  for (const Totals& totals : perThread) {
    detail::addCounts(std::span(totals.players.data(), numPlayers), result.players);
    result.boards += totals.boards;
  }
  return result;
}

uint64_t exactShowdowns(std::span<const Range> players, CardMask board, CardMask dead,
                        uint64_t limit) {
  const auto prepared = detail::liveHands(players, board, dead);
  if (std::holds_alternative<EquityError>(prepared)) return 0;
  const auto& hands = std::get<detail::LiveHands>(prepared);

  // Every deal is completed by the same number of boards, so count deals, stopping as soon as the
  // total passes the limit.
  const auto deckSize =
      static_cast<uint64_t>(detail::cardsLeftAfterDeal(board, dead, hands.size()));
  const uint64_t boardsPerDeal = binomial(deckSize, detail::missingBoardCards(board));
  const uint64_t maxDeals = std::min(limit / boardsPerDeal, kMaxExactDeals);
  uint64_t deals = 0;
  detail::forEachDeal(hands, [&](const detail::Seats&) { return ++deals <= maxDeals; });
  if (deals > kMaxExactDeals) return std::numeric_limits<uint64_t>::max();  // exactEquity() refuses
  return deals * boardsPerDeal;
}

}  // namespace poker
