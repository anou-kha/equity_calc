#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <ostream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "poker/card.hpp"
#include "poker/evaluator.hpp"
#include "test_helpers.hpp"

namespace {

using poker::CardMask;
using poker::HandCategory;
using poker::HandRank;
using poker::test::cards;
using poker::test::randomHand;

HandRank eval(std::string_view text) { return poker::evaluate(cards(text)); }

// Calls visit(hand) for every `size`-card subset of the deck, each exactly once.
template <typename Visit>
void forEachHand(int size, const Visit& visit, int firstCard = 0, CardMask hand = 0) {
  if (size == 0) {
    visit(hand);
    return;
  }
  for (int card = firstCard; card <= poker::Card::kNumCards - size; ++card) {
    forEachHand(size - 1, visit, card + 1, hand | (CardMask{1} << card));
  }
}

// ---- Category detection ----------------------------------------------------------------------

struct CategoryCase {
  const char* name;  // becomes part of the test name
  const char* cards;
  HandCategory category;
  const char* description;
};

void PrintTo(const CategoryCase& c, std::ostream* os) { *os << c.cards; }

constexpr CategoryCase kCategoryCases[] = {
    {"RoyalFlush", "As Ks Qs Js Ts 9c 2d", HandCategory::kStraightFlush, "Royal flush"},
    {"SteelWheel", "Ah 2h 3h 4h 5h Kd Kc", HandCategory::kStraightFlush,
     "Straight flush, Five high"},
    {"SevenSuitedCardsInARow", "9h 8h 7h 6h 5h 4h 3h", HandCategory::kStraightFlush,
     "Straight flush, Nine high"},
    // The plain straight is Queen high, but the straight flush (Ten high) is the better hand.
    {"StraightFlushOverHigherStraight", "Th 9h 8h 7h 6h Jc Qd", HandCategory::kStraightFlush,
     "Straight flush, Ten high"},
    {"QuadsPickBestKicker", "7c 7d 7h 7s Kd Kc Ah", HandCategory::kFourOfAKind,
     "Four of a kind, Sevens (Ace kicker)"},
    {"QuadsWithTrips", "7c 7d 7h 7s Kd Kc Kh", HandCategory::kFourOfAKind,
     "Four of a kind, Sevens (King kicker)"},
    {"FullHouseFromTwoSetsOfTrips", "Kc Kd Kh 7c 7d 7h 2s", HandCategory::kFullHouse,
     "Full house, Kings full of Sevens"},
    {"FullHouseUsesHighestPair", "9c 9d 9h 3c 3d Ac Ad", HandCategory::kFullHouse,
     "Full house, Nines full of Aces"},
    {"FlushWithSixSuitedCards", "Ah Jh 9h 6h 4h 2h Kd", HandCategory::kFlush,
     "Flush, Ace-Jack-Nine-Six-Four"},
    {"FlushWithSevenSuitedCards", "Ah Kh 9h 7h 5h 3h 2h", HandCategory::kFlush,
     "Flush, Ace-King-Nine-Seven-Five"},
    // Nine-high straight and a heart flush, but the hearts aren't consecutive.
    {"FlushAndStraightButNotStraightFlush", "Kh 9h 7h 5h 2h 8c 6d", HandCategory::kFlush,
     "Flush, King-Nine-Seven-Five-Two"},
    {"Wheel", "Ac 2d 3h 4s 5c Kd 9h", HandCategory::kStraight, "Straight, Five high"},
    {"AceHighStraight", "Ac Kd Qh Js Tc 3d 2h", HandCategory::kStraight, "Straight, Ace high"},
    {"SixCardRunUsesHighestStraight", "4c 5d 6h 7s 8c 9d Ah", HandCategory::kStraight,
     "Straight, Nine high"},
    {"StraightWithPairedCard", "5c 6d 7h 8s 9c 9d 2h", HandCategory::kStraight,
     "Straight, Nine high"},
    {"ThreeOfAKind", "Qc Qd Qh 9s 7c 4d 2h", HandCategory::kThreeOfAKind,
     "Three of a kind, Queens (Nine, Seven kickers)"},
    // Three pairs: the top two play and the third pair's rank is the kicker.
    {"TwoPairFromThreePairs", "Ac Ad Kc Kd Qc Qd 2h", HandCategory::kTwoPair,
     "Two pair, Aces and Kings (Queen kicker)"},
    // Three pairs again, but a single Ace outranks the third pair as the kicker.
    {"TwoPairFromThreePairsAceKicker", "Jc Jd 8c 8d 4c 4d Ah", HandCategory::kTwoPair,
     "Two pair, Jacks and Eights (Ace kicker)"},
    {"Pair", "Tc Td Ah 9s 4c 3d 2h", HandCategory::kPair, "Pair of Tens (Ace, Nine, Four kickers)"},
    {"HighCard", "Ah Kd 9c 7s 4h 3d 2c", HandCategory::kHighCard,
     "High card, Ace-King-Nine-Seven-Four"},
    {"FiveCardInput", "As Ks Qs Js Ts", HandCategory::kStraightFlush, "Royal flush"},
    {"SixCardInput", "2c 2d 5h 5s 9c Jd", HandCategory::kTwoPair,
     "Two pair, Fives and Twos (Jack kicker)"},
};

class EvaluatorCategory : public testing::TestWithParam<CategoryCase> {};

TEST_P(EvaluatorCategory, DetectsCategoryAndDescribesHand) {
  const CategoryCase& c = GetParam();
  const HandRank rank = eval(c.cards);
  EXPECT_EQ(poker::category(rank), c.category);
  EXPECT_EQ(poker::describe(rank), c.description);
}

INSTANTIATE_TEST_SUITE_P(Hands, EvaluatorCategory, testing::ValuesIn(kCategoryCases),
                         [](const testing::TestParamInfo<CategoryCase>& info) {
                           return std::string(info.param.name);
                         });

TEST(Evaluator, CategoryNames) {
  EXPECT_EQ(poker::categoryName(HandCategory::kHighCard), "High card");
  EXPECT_EQ(poker::categoryName(HandCategory::kPair), "Pair");
  EXPECT_EQ(poker::categoryName(HandCategory::kTwoPair), "Two pair");
  EXPECT_EQ(poker::categoryName(HandCategory::kThreeOfAKind), "Three of a kind");
  EXPECT_EQ(poker::categoryName(HandCategory::kStraight), "Straight");
  EXPECT_EQ(poker::categoryName(HandCategory::kFlush), "Flush");
  EXPECT_EQ(poker::categoryName(HandCategory::kFullHouse), "Full house");
  EXPECT_EQ(poker::categoryName(HandCategory::kFourOfAKind), "Four of a kind");
  EXPECT_EQ(poker::categoryName(HandCategory::kStraightFlush), "Straight flush");
}

// ---- Ordering ---------------------------------------------------------------------------------

TEST(EvaluatorOrdering, KickersDecideHighCardHands) {
  EXPECT_GT(eval("Ah Kd 9c 7s 4h"), eval("Ac Qd 9h 7d 4s"));  // AK high beats AQ high
  EXPECT_GT(eval("Ah Kd 9c 7s 4h"), eval("Ac Kh 9h 7d 3s"));  // even the fifth card counts
}

TEST(EvaluatorOrdering, KickersDecidePairs) {
  EXPECT_GT(eval("Ah Ad Kc 9s 4h"), eval("As Ac Qd 9h 4d"));
  EXPECT_GT(eval("Ah Ad Kc 9s 4h"), eval("As Ac Kd 9h 3d"));
}

TEST(EvaluatorOrdering, HigherTwoPairWins) {
  EXPECT_GT(eval("Ac Ad 3c 3d 9h"), eval("Kc Kd Qc Qd Ah"));  // the top pair decides first,
  EXPECT_GT(eval("Kc Kd Qc Qd 2h"), eval("Kh Ks Jc Jd Ah"));  // then the second pair,
  EXPECT_GT(eval("Ac Ad 3c 3d 9h"), eval("Ah As 3h 3s 8d"));  // then the kicker.
}

TEST(EvaluatorOrdering, FullHouseComparesTripsBeforePair) {
  EXPECT_GT(eval("3c 3d 3h 2s 2h"), eval("2c 2d 2h Ac Ad"));  // threes full beats twos full
}

TEST(EvaluatorOrdering, WheelLosesToSixHighStraight) {
  EXPECT_GT(eval("6c 5d 4h 3s 2c"), eval("Ac 2d 3h 4s 5c"));
  EXPECT_GT(eval("6c 5c 4c 3c 2c"), eval("Ac 2c 3c 4c 5c"));  // same for straight flushes
}

// Packing the category above the kickers means even the best hand of one category loses to the
// worst hand of the next.
TEST(EvaluatorOrdering, WeakestOfEachCategoryBeatsStrongestOfThePrevious) {
  const std::array<std::pair<const char*, const char*>, 8> kStrongestThenWeakest = {{
      {"Ah Kd Qc Js 9h", "2c 2d 3h 4s 5c"},  // high card < pair
      {"Ah Ad Kc Qs Jh", "3c 3d 2h 2s 4c"},  // pair < two pair
      {"Ah Ad Kc Ks Qh", "2c 2d 2h 3s 4c"},  // two pair < three of a kind
      {"Ah Ad Ac Ks Qh", "Ac 2d 3h 4s 5c"},  // three of a kind < straight (the wheel)
      {"Ah Kd Qc Js Th", "7c 5c 4c 3c 2c"},  // straight < flush
      {"Ah Kh Qh Jh 9h", "2c 2d 2h 3s 3c"},  // flush < full house
      {"Ah Ad Ac Ks Kh", "2c 2d 2h 2s 3c"},  // full house < four of a kind
      {"Ah Ad Ac As Kh", "Ac 2c 3c 4c 5c"},  // four of a kind < straight flush (steel wheel)
  }};
  for (const auto& [strongest, weakest] : kStrongestThenWeakest) {
    EXPECT_LT(eval(strongest), eval(weakest)) << strongest << " vs " << weakest;
  }
}

TEST(EvaluatorOrdering, DefaultHandRankIsBelowEveryHand) {
  EXPECT_LT(HandRank{}, eval("7c 5d 4h 3s 2c"));  // the weakest possible hand
}

// ---- Ties -------------------------------------------------------------------------------------

TEST(EvaluatorTies, BoardPlays) {
  // A royal flush on the board: the hole cards don't matter.
  EXPECT_EQ(eval("As Ks Qs Js Ts 2c 3d"), eval("As Ks Qs Js Ts 4h 5h"));
  // A straight on the board beats both pocket pairs, so both players play the board.
  EXPECT_EQ(eval("5c 6d 7h 8s 9c Ah Ad"), eval("5c 6d 7h 8s 9c Kc Kd"));
  // Two pair on the board, and the board's Queen kicker beats every hole card.
  EXPECT_EQ(eval("Ac Ad Kc Kd Qh 2c 3d"), eval("Ac Ad Kc Kd Qh Jh 9h"));
}

TEST(EvaluatorTies, SixthAndSeventhCardsDoNotPlay) {
  EXPECT_EQ(eval("Ah Kd Qc Js 9h 3d 2c"), eval("Ac Kh Qd Jc 9s 4h 2d"));
}

TEST(EvaluatorTies, SuitsDoNotMatter) { EXPECT_EQ(eval("Ac Kd 9h 7s 4c"), eval("Ad Kh 9s 7c 4d")); }

// ---- Exhaustive checks ------------------------------------------------------------------------

// Tallies hands per category, royal flushes, and the number of distinct HandRank values.
class Census {
 public:
  void add(HandRank rank) {
    ++counts_[static_cast<size_t>(poker::category(rank))];
    if (rank == royal_) ++royalFlushes_;
    distinct_.insert(rank.value());
  }

  int64_t count(HandCategory c) const { return counts_[static_cast<size_t>(c)]; }
  int64_t royalFlushes() const { return royalFlushes_; }
  size_t distinctRanks() const { return distinct_.size(); }

 private:
  HandRank royal_ = eval("Ah Kh Qh Jh Th");
  std::array<int64_t, poker::kNumHandCategories> counts_{};
  int64_t royalFlushes_ = 0;
  std::unordered_set<uint32_t> distinct_;
};

// Checks a census against known frequencies, listed in HandCategory order.
void expectCounts(const Census& census,
                  const std::array<int64_t, poker::kNumHandCategories>& expected) {
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto c = static_cast<HandCategory>(i);
    EXPECT_EQ(census.count(c), expected[i]) << poker::categoryName(c);
  }
}

// Known frequencies are from https://en.wikipedia.org/wiki/Poker_probability. That table lists
// royal flushes separately from other straight flushes, but evaluate() puts them in the straight
// flush category, so the straight flush counts here include them.
TEST(EvaluatorExhaustive, AllFiveCardHandsMatchKnownFrequencies) {
  Census census;
  forEachHand(5, [&](CardMask hand) { census.add(poker::evaluate(hand)); });

  expectCounts(census, {1'302'540, 1'098'240, 123'552, 54'912, 10'200, 5'108, 3'744, 624,
                        36 + 4});  // 36 straight flushes + 4 royal flushes
  EXPECT_EQ(census.royalFlushes(), 4);
  EXPECT_EQ(census.distinctRanks(), 7'462u);
}

// All C(52,7) = 133,784,560 seven-card hands. This takes seconds in Release and much longer in
// Debug, so it is disabled by default. Run it with:
//   poker_tests --gtest_also_run_disabled_tests --gtest_filter=*AllSevenCard*
TEST(EvaluatorExhaustive, DISABLED_AllSevenCardHandsMatchKnownFrequencies) {
  Census census;
  forEachHand(7, [&](CardMask hand) { census.add(poker::evaluate(hand)); });

  expectCounts(census, {23'294'460, 58'627'800, 31'433'400, 6'461'620, 6'180'020, 4'047'644,
                        3'473'184, 224'848, 37'260 + 4'324});  // straight flushes + royal flushes
  EXPECT_EQ(census.royalFlushes(), 4'324);
  EXPECT_EQ(census.distinctRanks(), 4'824u);
}

// ---- A second method --------------------------------------------------------------------------

// Evaluates a 7-card hand the long way: the best of the C(7,2) = 21 five-card hands left after
// dropping two cards. The 5-card logic is already checked exhaustively above, so agreement here
// checks the 7-card logic: choosing the best five cards and the right kickers.
HandRank bestFiveCardSubset(CardMask hand) {
  std::vector<CardMask> singleCards;
  for (CardMask rest = hand; rest != 0; rest &= rest - 1) {
    singleCards.push_back(CardMask{1} << std::countr_zero(rest));
  }
  HandRank best;
  for (size_t i = 0; i < singleCards.size(); ++i) {
    for (size_t j = i + 1; j < singleCards.size(); ++j) {
      best = std::max(best, poker::evaluate(hand & ~(singleCards[i] | singleCards[j])));
    }
  }
  return best;
}

TEST(EvaluatorConsistency, SevenCardRankIsBestFiveCardSubset) {
  std::mt19937_64 rng(20261119);
  for (int i = 0; i < 100'000; ++i) {
    const CardMask hand = randomHand(rng, 7);
    ASSERT_EQ(poker::evaluate(hand), bestFiveCardSubset(hand)) << "hand mask " << hand;
  }
}

}  // namespace
