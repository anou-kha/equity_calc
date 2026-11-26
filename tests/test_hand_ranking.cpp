// The hand ranking (hand_ranking.hpp). The stored table must hold the 169 classes once each, best
// first, with counts that match the Wizard of Odds' exhaustive results and this calculator.
// topRange() must take whole classes in that order, as close as possible to the share asked for.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/equity.hpp"
#include "poker/hand_ranking.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/range.hpp"
#include "test_helpers.hpp"  // prints a Range readably when a test fails

namespace {

using poker::Range;
using poker::RankedClass;

Range rangeOf(std::string_view text) { return std::get<Range>(poker::parseRange(text)); }

// ---- The table --------------------------------------------------------------------------------

TEST(HandRanking, HasEveryClassOnce) {
  const auto ranking = poker::handRanking();
  ASSERT_EQ(ranking.size(), 169u);
  std::set<std::string_view> names;
  Range all;
  for (const RankedClass& handClass : ranking) {
    SCOPED_TRACE(handClass.name);
    EXPECT_TRUE(names.insert(handClass.name).second) << "listed twice";
    const Range hands = rangeOf(handClass.name);
    EXPECT_EQ(hands.size(), handClass.hands());
    for (const auto& hand : hands.combos()) all.add(hand);
  }
  EXPECT_EQ(all, rangeOf("random"));  // together, every hand
}

TEST(HandRanking, IsBestFirstWithNoTwoEqual) {
  const auto ranking = poker::handRanking();
  for (size_t i = 1; i < ranking.size(); ++i) {
    SCOPED_TRACE(ranking[i].name);
    // Compared exactly, as twice the pot share in showdowns.
    EXPECT_GT(2 * ranking[i - 1].wins + ranking[i - 1].ties, 2 * ranking[i].wins + ranking[i].ties);
    EXPECT_LE(ranking[i].wins + ranking[i].ties, RankedClass::kBoards);
  }
}

// The Wizard of Odds' exhaustive two-player table (see test_range_equity.cpp) lists AA against a
// random hand as win 10,689,050,508, lose 1,827,970,020 and draw 68,413,872, summed over the 6 AA
// hands. Each contributes exactly a sixth.
TEST(HandRanking, AcesAreFirstAndMatchWizardOfOddsExactly) {
  const RankedClass& aces = poker::handRanking().front();
  EXPECT_EQ(aces.name, "AA");
  EXPECT_EQ(6 * aces.wins, 10'689'050'508u);
  EXPECT_EQ(6 * (RankedClass::kBoards - aces.wins - aces.ties), 1'827'970'020u);
  EXPECT_EQ(6 * aces.ties, 68'413'872u);
  EXPECT_NEAR(aces.equity(), 0.8520, 0.00005);
}

// Against the Wizard of Odds' rounded percentages: 72o wins 31.71% and draws 5.75%. It's often
// called the worst hand, because its cards are too far apart to make a straight together, but
// heads-up the high card counts for more, and 32o comes last.
TEST(HandRanking, SevenDeuceIsNotTheWorstHeadsUp) {
  const auto ranking = poker::handRanking();
  const auto sevenDeuce =
      std::find_if(ranking.begin(), ranking.end(), [](const auto& c) { return c.name == "72o"; });
  ASSERT_NE(sevenDeuce, ranking.end());
  const auto boards = static_cast<double>(RankedClass::kBoards);
  EXPECT_NEAR(static_cast<double>(sevenDeuce->wins) / boards, 0.3171, 0.00005);
  EXPECT_NEAR(static_cast<double>(sevenDeuce->ties) / boards, 0.0575, 0.00005);
  EXPECT_EQ(ranking.back().name, "32o");
}

// Every class against Monte Carlo, with 20,000 trials each: a check, independent of how the tool
// chose each class's hand, that every row belongs to the class it's listed under. Swapping suited
// and offsuit, for example, would move most rows by more than the tolerance.
TEST(HandRanking, AgreesWithMonteCarloForEveryClass) {
  const Range random = rangeOf("random");
  for (const RankedClass& handClass : poker::handRanking()) {
    SCOPED_TRACE(handClass.name);
    const std::vector<Range> players = {rangeOf(handClass.name), random};
    const auto result = std::get<poker::MonteCarloResult>(
        poker::monteCarloEquity(players, 0, 0, {.trials = 20'000, .seed = 3}));
    const poker::Estimate equity = result.equity(0);
    // Four standard errors (two 95% margins): a false alarm somewhere in 169 classes is unlikely.
    EXPECT_NEAR(handClass.equity(), equity.value, 2 * equity.margin95());
  }
}

// Recomputes a few classes exactly, as tools/rank_hands.cpp does, but with a different hand of the
// class: the first in canonical order (in clubs and diamonds), where the tool used spades and
// hearts. Matching counts confirm both the table and the suit symmetry it relies on. About 10 s
// per class in Release on 16 threads, so this is opt-in:
//   poker_tests --gtest_also_run_disabled_tests --gtest_filter=*RecomputedExactly*
TEST(HandRanking, DISABLED_RecomputedExactlyMatchesTheTable) {
  const auto ranking = poker::handRanking();
  for (const size_t index : {size_t{1}, size_t{84}, size_t{168}}) {
    const RankedClass& handClass = ranking[index];
    SCOPED_TRACE(handClass.name);
    Range hand;
    hand.add(rangeOf(handClass.name).combos().front());
    const std::vector<Range> players = {hand, rangeOf("random")};
    const auto result =
        std::get<poker::EquityResult>(poker::exactEquity(players, 0, 0, {.threads = 0}));
    EXPECT_EQ(result.boards, RankedClass::kBoards);
    EXPECT_EQ(result.players[0].wins, handClass.wins);
    EXPECT_EQ(result.players[0].ties, handClass.ties);
  }
}

// ---- topRange ---------------------------------------------------------------------------------

TEST(TopRange, NothingAndEverything) {
  EXPECT_TRUE(poker::topRange(0).empty());
  EXPECT_TRUE(poker::topRange(-5).empty());
  EXPECT_TRUE(poker::topRange(std::numeric_limits<double>::quiet_NaN()).empty());
  EXPECT_EQ(poker::topRange(100), rangeOf("random"));
  EXPECT_EQ(poker::topRange(250), rangeOf("random"));
}

TEST(TopRange, SmallestRanges) {
  EXPECT_TRUE(poker::topRange(0.2).empty());  // AA would be 6 hands, further from 2.7 than 0 is
  EXPECT_EQ(poker::formatRange(poker::topRange(0.5)), "AA");  // 6 hands, 0.45%
  EXPECT_EQ(poker::formatRange(poker::topRange(1)), "KK+");   // 12 hands, 0.9%
}

// For every percentage in steps of 0.25: the best whole classes, and the closest count of hands.
TEST(TopRange, TakesTheBestClassesClosestToTheTarget) {
  // best[k]: the hands of the best k classes, for k = 0 to 169.
  std::vector<Range> best(1);
  for (const RankedClass& handClass : poker::handRanking()) {
    Range next = best.back();
    const Range hands = rangeOf(handClass.name);
    for (const auto& hand : hands.combos()) next.add(hand);
    best.push_back(next);
  }

  size_t previousClasses = 0;
  for (int quarters = 0; quarters <= 400; ++quarters) {
    const double percent = quarters / 4.0;
    SCOPED_TRACE(percent);
    const Range range = poker::topRange(percent);
    const auto classes =
        static_cast<size_t>(std::find(best.begin(), best.end(), range) - best.begin());
    ASSERT_LT(classes, best.size()) << "not the best few classes";
    EXPECT_GE(classes, previousClasses);  // a bigger percentage never gives fewer hands
    previousClasses = classes;

    // No other number of classes comes closer (one more or one fewer is enough to check, since
    // the distance falls and then rises), and a tie goes to the smaller range.
    const double target = percent / 100.0 * static_cast<double>(Range::kNumCombos);
    const auto distance = [&](size_t k) {
      return std::abs(static_cast<double>(best[k].size()) - target);
    };
    if (classes > 0) EXPECT_LE(distance(classes), distance(classes - 1));
    if (classes + 1 < best.size()) EXPECT_LT(distance(classes), distance(classes + 1));
  }
}

}  // namespace
