// Naive reference hand evaluator.
//
// Idea: summarise the cards as a few 13-bit "rank sets" (which ranks appear at least once, twice,
// three times, four times, and which ranks each suit holds), then test the categories from
// strongest to weakest and return the first that matches. The winning category and its deciding
// ranks are packed into one integer (see HandRank in evaluator.hpp) so hands compare with `<`.
//
// Trade-off: this does a little work per rank and per category on every call, which is slow
// compared with lookup-table evaluators, but every step maps directly onto a poker rule. That makes
// it a trustworthy reference: the fast evaluator (fast_evaluator.cpp) builds its tables with it and
// is tested against it.
#include "poker/evaluator.hpp"

#include <array>
#include <bit>
#include <cassert>

namespace poker {
namespace {

// A set of ranks: bit r is set when rank r (0 = '2' ... 12 = 'A') is present. 13 bits are used.
using RankSet = uint32_t;

constexpr int kFive = 3;
constexpr int kSix = 4;
constexpr int kAce = 12;
constexpr int kNoStraight = -1;

constexpr int kBitsPerRank = 4;
constexpr int kRankSlots = 5;
constexpr int kCategoryShift = kBitsPerRank * kRankSlots;  // the category sits above the ranks
constexpr uint32_t kRankMask = 0xF;

constexpr RankSet rankBit(int rank) { return RankSet{1} << rank; }

// The highest rank in a non-empty set.
int highestRank(RankSet ranks) {
  assert(ranks != 0);
  return static_cast<int>(std::bit_width(ranks)) - 1;
}

RankSet withoutRank(RankSet ranks, int rank) { return ranks & ~rankBit(rank); }

// Everything the category checks need to know about a set of cards.
struct CardSummary {
  RankSet ranks = 0;                                 // ranks present at least once
  RankSet pairs = 0;                                 // ranks present at least twice
  RankSet trips = 0;                                 // ranks present at least three times
  RankSet quads = 0;                                 // ranks present four times
  std::array<RankSet, Card::kNumSuits> suitRanks{};  // the ranks held in each suit
};

CardSummary summarize(CardMask cards) {
  CardSummary summary;
  for (int rank = 0; rank < Card::kNumRanks; ++rank) {
    // Card index = rank * 4 + suit, so the four cards of one rank are four adjacent bits, one per
    // suit. Shifting them down gives a 4-bit "which suits are present" value for this rank.
    const auto suits = static_cast<uint32_t>((cards >> (rank * Card::kNumSuits)) & 0xF);
    const int count = std::popcount(suits);
    const RankSet bit = rankBit(rank);
    if (count >= 1) summary.ranks |= bit;
    if (count >= 2) summary.pairs |= bit;
    if (count >= 3) summary.trips |= bit;
    if (count == 4) summary.quads |= bit;
    for (size_t suit = 0; suit < summary.suitRanks.size(); ++suit) {
      if (suits & (1u << suit)) summary.suitRanks[suit] |= bit;
    }
  }
  return summary;
}

// The ranks of the flush suit, or an empty set if there is no flush. With at most 7 cards, at most
// one suit can hold 5 or more of them.
RankSet flushRanks(const CardSummary& summary) {
  for (RankSet suited : summary.suitRanks) {
    if (std::popcount(suited) >= 5) return suited;
  }
  return 0;
}

// The high card of the best straight in `ranks`, or kNoStraight.
int straightHigh(RankSet ranks) {
  constexpr RankSet kFiveInARow = 0b11111;
  // Slide a five-rank window from ace-high (A K Q J T) down to six-high (6 5 4 3 2).
  for (int high = kAce; high >= kSix; --high) {
    const RankSet window = kFiveInARow << (high - 4);
    if ((ranks & window) == window) return high;
  }
  // The wheel A-2-3-4-5 is the one straight where the ace plays low, so its high card is the 5.
  constexpr RankSet kWheel = rankBit(kAce) | 0b1111;  // A plus 2, 3, 4, 5
  if ((ranks & kWheel) == kWheel) return kFive;
  return kNoStraight;
}

// Builds a HandRank by filling its five rank slots from most to least important.
class RankBuilder {
 public:
  explicit RankBuilder(HandCategory category)
      : value_(static_cast<uint32_t>(category) << kCategoryShift) {}

  RankBuilder& add(int rank) {
    assert(slotsUsed_ < kRankSlots);
    ++slotsUsed_;
    value_ |= static_cast<uint32_t>(rank) << (kCategoryShift - kBitsPerRank * slotsUsed_);
    return *this;
  }

  // Adds the `count` highest ranks of `ranks`: the kickers, or the five cards of a flush.
  RankBuilder& addHighest(RankSet ranks, int count) {
    for (int i = 0; i < count; ++i) {
      const int rank = highestRank(ranks);
      add(rank);
      ranks = withoutRank(ranks, rank);
    }
    return *this;
  }

  HandRank build() const { return HandRank(value_); }

 private:
  uint32_t value_;
  int slotsUsed_ = 0;
};

// The rank stored in slot 0..4 of a HandRank.
int rankInSlot(HandRank rank, int slot) {
  const int shift = kCategoryShift - kBitsPerRank * (slot + 1);
  return static_cast<int>((rank.value() >> shift) & kRankMask);
}

constexpr std::array<std::string_view, kNumHandCategories> kCategoryNames = {
    "High card", "Pair",       "Two pair",       "Three of a kind", "Straight",
    "Flush",     "Full house", "Four of a kind", "Straight flush",
};

constexpr std::array<std::string_view, Card::kNumRanks> kRankNames = {
    "Two",  "Three", "Four", "Five",  "Six",  "Seven", "Eight",
    "Nine", "Ten",   "Jack", "Queen", "King", "Ace",
};

constexpr std::array<std::string_view, Card::kNumRanks> kRankPlurals = {
    "Twos",  "Threes", "Fours", "Fives",  "Sixes", "Sevens", "Eights",
    "Nines", "Tens",   "Jacks", "Queens", "Kings", "Aces",
};

std::string rankName(int rank) { return std::string(kRankNames[static_cast<size_t>(rank)]); }
std::string rankPlural(int rank) { return std::string(kRankPlurals[static_cast<size_t>(rank)]); }

// "Ace-King-Nine-Seven-Four": all five ranks, for hands where every card counts equally.
std::string allFiveRanks(HandRank rank) {
  std::string text = rankName(rankInSlot(rank, 0));
  for (int slot = 1; slot < kRankSlots; ++slot) text += "-" + rankName(rankInSlot(rank, slot));
  return text;
}

// " (Ace, Nine kickers)": the ranks in slots [first, first + count).
std::string kickers(HandRank rank, int first, int count) {
  std::string text = " (";
  for (int slot = first; slot < first + count; ++slot) {
    if (slot > first) text += ", ";
    text += rankName(rankInSlot(rank, slot));
  }
  return text + (count == 1 ? " kicker)" : " kickers)");
}

}  // namespace

HandRank evaluateNaive(CardMask cards) {
  assert(std::popcount(cards) >= 5 && std::popcount(cards) <= 7);
  const CardSummary s = summarize(cards);
  const RankSet flush = flushRanks(s);

  // Categories are checked from strongest to weakest; the first match is the answer.
  if (flush != 0) {
    // Look for the straight inside the flush suit only, not among all ranks.
    if (const int high = straightHigh(flush); high != kNoStraight) {
      return RankBuilder(HandCategory::kStraightFlush).add(high).build();
    }
  }
  if (s.quads != 0) {
    const int quad = highestRank(s.quads);
    return RankBuilder(HandCategory::kFourOfAKind)
        .add(quad)
        .addHighest(withoutRank(s.ranks, quad), 1)
        .build();
  }
  if (s.trips != 0) {
    const int trips = highestRank(s.trips);
    // Any other rank held at least twice completes the full house, including a second set of trips.
    const RankSet pairs = withoutRank(s.pairs, trips);
    if (pairs != 0) {
      return RankBuilder(HandCategory::kFullHouse).add(trips).add(highestRank(pairs)).build();
    }
  }
  if (flush != 0) {
    return RankBuilder(HandCategory::kFlush).addHighest(flush, 5).build();
  }
  if (const int high = straightHigh(s.ranks); high != kNoStraight) {
    return RankBuilder(HandCategory::kStraight).add(high).build();
  }
  if (s.trips != 0) {
    const int trips = highestRank(s.trips);
    return RankBuilder(HandCategory::kThreeOfAKind)
        .add(trips)
        .addHighest(withoutRank(s.ranks, trips), 2)
        .build();
  }
  if (std::popcount(s.pairs) >= 2) {
    // With three pairs, only the top two count; the third pair's rank can still be the kicker.
    const int high = highestRank(s.pairs);
    const int low = highestRank(withoutRank(s.pairs, high));
    return RankBuilder(HandCategory::kTwoPair)
        .add(high)
        .add(low)
        .addHighest(withoutRank(withoutRank(s.ranks, high), low), 1)
        .build();
  }
  if (s.pairs != 0) {
    const int pair = highestRank(s.pairs);
    return RankBuilder(HandCategory::kPair)
        .add(pair)
        .addHighest(withoutRank(s.ranks, pair), 3)
        .build();
  }
  return RankBuilder(HandCategory::kHighCard).addHighest(s.ranks, 5).build();
}

HandCategory category(HandRank rank) {
  return static_cast<HandCategory>(rank.value() >> kCategoryShift);
}

std::string_view categoryName(HandCategory c) { return kCategoryNames[static_cast<size_t>(c)]; }

std::string describe(HandRank rank) {
  const int first = rankInSlot(rank, 0);
  const int second = rankInSlot(rank, 1);
  switch (category(rank)) {
    case HandCategory::kStraightFlush:
      return first == kAce ? "Royal flush" : "Straight flush, " + rankName(first) + " high";
    case HandCategory::kFourOfAKind:
      return "Four of a kind, " + rankPlural(first) + kickers(rank, 1, 1);
    case HandCategory::kFullHouse:
      return "Full house, " + rankPlural(first) + " full of " + rankPlural(second);
    case HandCategory::kFlush:
      return "Flush, " + allFiveRanks(rank);
    case HandCategory::kStraight:
      return "Straight, " + rankName(first) + " high";
    case HandCategory::kThreeOfAKind:
      return "Three of a kind, " + rankPlural(first) + kickers(rank, 1, 2);
    case HandCategory::kTwoPair:
      return "Two pair, " + rankPlural(first) + " and " + rankPlural(second) + kickers(rank, 2, 1);
    case HandCategory::kPair:
      return "Pair of " + rankPlural(first) + kickers(rank, 1, 3);
    case HandCategory::kHighCard:
      return "High card, " + allFiveRanks(rank);
  }
  return "Invalid hand rank";  // only reachable for values evaluate() never produces
}

}  // namespace poker
