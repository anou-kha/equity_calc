// Fast lookup-table hand evaluator.
//
// Idea: do the naive evaluator's work once, ahead of time, for every situation that can occur, and
// store the answers in tables. Evaluating a hand is then just computing a table index.
//
// Two facts keep the tables small:
//  1. With 5-7 cards a flush can't coexist with a full house or quads. So if a suit holds 5 or more
//     cards, the answer depends only on which ranks that suit holds: a 13-bit rank set indexes the
//     flush table (8,192 entries).
//  2. Otherwise suits don't matter at all, only how many cards of each rank there are. A perfect
//     hash maps those 13 counts to a dense index: for 7 cards there are only 49,205 possible count
//     vectors (6,175 for 5 cards, 18,395 for 6), so the tables have no gaps and no collisions.
//
// Every table entry is filled in by asking evaluateNaive() about one representative hand, so
// evaluate() returns exactly the same HandRank values as the reference implementation.
//
// Trade-off: about 330 KB of tables and a few milliseconds to build them on first use, in exchange
// for a few bit operations, at most 13 additions and one or two table reads per hand. Designs with
// bigger tables (like the ~130 MB "Two Plus Two" state machine) are faster still, but too big for
// the planned WebAssembly build.
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "poker/evaluator.hpp"

namespace poker {
namespace {

constexpr size_t kNumRanks = 13;
constexpr size_t kNumSuits = 4;
constexpr size_t kMinCards = 5;
constexpr size_t kMaxCards = 7;
constexpr size_t kMaxPerRank = 4;

// The clubs: bit 4 * rank for every rank. Shifting left by `suit` gives any other suit's cards.
constexpr CardMask kClubs = 0x1111111111111;

// The number of cards of each rank, packed into nibbles: nibble r holds the count for rank r.
// Each rank's four cards are four adjacent bits (one nibble), so this is a popcount of every nibble
// at once: first count the set bits in each pair of bits, then add neighbouring pairs. (These are
// the first two steps of the classic bit-counting trick.)
uint64_t rankCounts(CardMask cards) {
  const uint64_t pairCounts = cards - ((cards >> 1) & 0x5555555555555555);
  return (pairCounts & 0x3333333333333333) + ((pairCounts >> 2) & 0x3333333333333333);
}

// The number of cards of one suit. That suit's cards, shifted down, are one bit per rank at bits 0,
// 4, 8, ..., 48. Multiplying by kClubs (0x1111...1) adds every nibble into all the nibbles above
// it, so the nibble at bits 48-51 ends up holding the sum of all 13: the suit's card count. The sum
// is at most 13, so it never overflows a nibble. This avoids std::popcount, which compilers turn
// into a slow library call unless the build allows the CPU's popcount instruction.
size_t suitCount(CardMask cards, size_t suit) {
  return static_cast<size_t>(((((cards >> suit) & kClubs) * kClubs) >> 48) & 0xF);
}

// The ranks held in one suit, as a 13-bit set. This loop only runs for flushes (about 3% of 7-card
// hands), so it doesn't need to be clever.
uint32_t suitRanks(CardMask cards, size_t suit) {
  // This suit's cards, moved down to bits 0, 4, 8, ..., 48.
  const CardMask suited = (cards >> suit) & kClubs;
  uint32_t ranks = 0;
  for (size_t rank = 0; rank < kNumRanks; ++rank) {
    ranks |= static_cast<uint32_t>((suited >> (4 * rank)) & 1) << rank;
  }
  return ranks;
}

// ---- The perfect hash -------------------------------------------------------------------------
//
// List every rank-count vector with a given total in lexicographic order (rank 0's count first,
// smaller counts first) and number them 0, 1, 2, ... A vector's number is how many vectors come
// before it. Going rank by rank, the vectors that come before it are those that agree on all
// earlier ranks but have a smaller count at this rank, with any valid counts after it. Counting
// those needs only a small precomputed table, so the hash is one addition per rank.

// waysToFill[r][n]: how many ways ranks r..12 can hold exactly n cards, at most 4 of each rank.
using WaysTable = std::array<std::array<uint32_t, kMaxCards + 1>, kNumRanks + 1>;

// offsets[r][n][c]: with n cards left for ranks r..12, how many vectors put fewer than c cards on
// rank r. Adding these up over the ranks gives a vector's position in the list.
using OffsetTable =
    std::array<std::array<std::array<uint32_t, kMaxPerRank + 1>, kMaxCards + 1>, kNumRanks>;

WaysTable countWaysToFill() {
  WaysTable ways{};
  ways[kNumRanks][0] = 1;  // with no ranks left, holding 0 cards is the only possibility
  for (size_t rank = kNumRanks; rank-- > 0;) {
    for (size_t cards = 0; cards <= kMaxCards; ++cards) {
      for (size_t count = 0; count <= std::min(cards, kMaxPerRank); ++count) {
        ways[rank][cards] += ways[rank + 1][cards - count];
      }
    }
  }
  return ways;
}

OffsetTable buildOffsets(const WaysTable& ways) {
  OffsetTable offsets{};
  for (size_t rank = 0; rank < kNumRanks; ++rank) {
    for (size_t cardsLeft = 0; cardsLeft <= kMaxCards; ++cardsLeft) {
      uint32_t before = 0;
      for (size_t count = 0; count <= std::min(cardsLeft, kMaxPerRank); ++count) {
        offsets[rank][cardsLeft][count] = before;
        before += ways[rank + 1][cardsLeft - count];
      }
    }
  }
  return offsets;
}

// The perfect hash itself: the position of this hand's rank counts in the list of all count vectors
// with `numCards` cards.
//
// The loop always visits all 13 ranks, even after the last card has been counted (those steps add
// 0). Stopping early looks cheaper, but it stops at a different rank for every hand, so the CPU's
// branch predictor keeps guessing wrong. Measured: the fixed 13-step loop makes evaluate() about
// 1.6x faster (48 -> 79 M hands/s), and the compiler can unroll it completely.
uint32_t nonFlushIndex(const OffsetTable& offsets, CardMask cards, size_t numCards) {
  const uint64_t counts = rankCounts(cards);
  uint32_t index = 0;
  size_t cardsLeft = numCards;
  for (size_t rank = 0; rank < kNumRanks; ++rank) {
    const auto count = static_cast<size_t>((counts >> (4 * rank)) & 0xF);
    index += offsets[rank][cardsLeft][count];
    cardsLeft -= count;
  }
  return index;
}

// ---- Building the tables ----------------------------------------------------------------------

struct Tables {
  OffsetTable offsets{};
  std::array<HandRank, size_t{1} << kNumRanks> flush{};  // indexed by the flush suit's rank set
  std::array<std::vector<HandRank>, kMaxCards + 1> nonFlush;  // [number of cards][perfect hash]
};

// Calls visit(hand) once for every way of giving ranks rank..12 counts (0-4 each) that total
// `cardsLeft`, with a hand that has exactly those counts and no flush. Suits are dealt round-robin
// (clubs, diamonds, hearts, spades, clubs, ...): no suit gets more than 2 of 7 cards, and the cards
// of one rank always get different suits.
template <typename Visit>
void forEachCountVector(size_t cardsLeft, size_t rank, size_t dealt, CardMask hand,
                        const Visit& visit) {
  if (rank == kNumRanks) {
    if (cardsLeft == 0) visit(hand);
    return;
  }
  CardMask withRank = hand;
  for (size_t count = 0; count <= std::min(cardsLeft, kMaxPerRank); ++count) {
    if (count > 0) withRank |= CardMask{1} << (4 * rank + (dealt + count - 1) % kNumSuits);
    forEachCountVector(cardsLeft - count, rank + 1, dealt + count, withRank, visit);
  }
}

Tables buildTables() {
  Tables tables;
  const WaysTable ways = countWaysToFill();
  tables.offsets = buildOffsets(ways);

  // Flush table: one all-clubs hand for every set of 5, 6 or 7 ranks.
  for (uint32_t ranks = 0; ranks < tables.flush.size(); ++ranks) {
    const auto numRanks = static_cast<size_t>(std::popcount(ranks));
    if (numRanks < kMinCards || numRanks > kMaxCards) continue;
    CardMask hand = 0;
    for (size_t rank = 0; rank < kNumRanks; ++rank) {
      if (ranks & (1u << rank)) hand |= CardMask{1} << (4 * rank);
    }
    tables.flush[ranks] = evaluateNaive(hand);
  }

  // Non-flush tables: one hand for every rank-count vector.
  for (size_t numCards = kMinCards; numCards <= kMaxCards; ++numCards) {
    std::vector<HandRank>& table = tables.nonFlush[numCards];
    table.resize(ways[0][numCards]);
    forEachCountVector(numCards, 0, 0, 0, [&](CardMask hand) {
      const uint32_t index = nonFlushIndex(tables.offsets, hand, numCards);
      assert(table[index] == HandRank{});  // a perfect hash never maps two vectors to one slot
      table[index] = evaluateNaive(hand);
    });
  }
  return tables;
}

// Built the first time it's needed. C++ guarantees a function-local static is initialised exactly
// once, even if several threads get here at the same moment ("magic statics").
const Tables& tables() {
  static const Tables instance = buildTables();
  return instance;
}

}  // namespace

HandRank evaluate(CardMask cards) {
  assert(std::popcount(cards) >= 5 && std::popcount(cards) <= 7);
  const Tables& t = tables();
  size_t numCards = 0;
  for (size_t suit = 0; suit < kNumSuits; ++suit) {
    const size_t count = suitCount(cards, suit);
    if (count >= 5) return t.flush[suitRanks(cards, suit)];
    numCards += count;
  }
  return t.nonFlush[numCards][nonFlushIndex(t.offsets, cards, numCards)];
}

void buildEvaluatorTables() { static_cast<void>(tables()); }

namespace detail {

size_t nonFlushTableSize(int numCards) {
  return tables().nonFlush[static_cast<size_t>(numCards)].size();
}

}  // namespace detail

}  // namespace poker
