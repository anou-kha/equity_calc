// Range-vs-range equity: exact enumeration over every deal, and Monte Carlo with rejection
// sampling of hands.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/range.hpp"
#include "test_helpers.hpp"

namespace {

using poker::EquityError;
using poker::EquityResult;
using poker::Estimate;
using poker::MonteCarloResult;
using poker::PlayerResult;
using poker::Range;
using poker::test::cards;
using poker::test::holes;

std::vector<Range> ranges(std::initializer_list<std::string_view> texts) {
  std::vector<Range> players;
  for (const std::string_view text : texts) {
    const auto parsed = poker::parseRange(text);
    if (!std::holds_alternative<Range>(parsed)) {
      throw std::invalid_argument("bad range in test: " + std::string(text));
    }
    players.push_back(std::get<Range>(parsed));
  }
  return players;
}

EquityResult exact(const std::vector<Range>& players, std::string_view board = "",
                   std::string_view dead = "") {
  const auto outcome = poker::exactEquity(players, cards(board), cards(dead), {.threads = 0});
  if (const auto* error = std::get_if<EquityError>(&outcome)) {
    throw std::runtime_error("unexpected error: " + std::string(poker::toString(*error)));
  }
  return std::get<EquityResult>(outcome);
}

MonteCarloResult monteCarlo(const std::vector<Range>& players, std::string_view board,
                            uint64_t trials, uint64_t seed = 1, unsigned threads = 0) {
  const auto outcome = poker::monteCarloEquity(
      players, cards(board), 0, {.trials = trials, .seed = seed, .threads = threads});
  if (const auto* error = std::get_if<EquityError>(&outcome)) {
    throw std::runtime_error("unexpected error: " + std::string(poker::toString(*error)));
  }
  return std::get<MonteCarloResult>(outcome);
}

EquityResult exactKnown(std::initializer_list<std::string_view> hands) {
  return std::get<EquityResult>(poker::exactEquity(holes(hands), 0, 0, {.threads = 0}));
}

void expectSameCounts(const EquityResult& expected, const EquityResult& actual) {
  EXPECT_EQ(actual.boards, expected.boards);
  ASSERT_EQ(actual.players.size(), expected.players.size());
  for (size_t i = 0; i < expected.players.size(); ++i) {
    EXPECT_EQ(actual.players[i].wins, expected.players[i].wins) << "player " << i;
    EXPECT_EQ(actual.players[i].ties, expected.players[i].ties) << "player " << i;
    EXPECT_EQ(actual.players[i].tieShares, expected.players[i].tieShares) << "player " << i;
  }
}

// `parts` as (weight, result) pairs: returns the counts of sum(weight * result).
EquityResult weightedSum(std::initializer_list<std::pair<uint64_t, const EquityResult*>> parts) {
  EquityResult sum;
  sum.players.resize(parts.begin()->second->players.size());
  for (const auto& [weight, part] : parts) {
    sum.boards += weight * part->boards;
    for (size_t i = 0; i < sum.players.size(); ++i) {
      sum.players[i].wins += weight * part->players[i].wins;
      sum.players[i].ties += weight * part->players[i].ties;
      sum.players[i].tieShares += weight * part->players[i].tieShares;
    }
  }
  return sum;
}

// |estimate - truth| within 4 standard errors, plus `rounding` for a published, rounded truth.
void expectNear(const Estimate& estimate, double truth, double rounding = 0) {
  EXPECT_LE(std::abs(estimate.value - truth), 4 * estimate.standardError + rounding)
      << "estimate " << estimate.value << " vs " << truth;
}

// ---- Exact: reference spots ---------------------------------------------------------------------

// Every AA hand faces 1 KK hand sharing both its suits, 4 sharing one and 1 sharing none, and suits
// are symmetric, so the range result must be exactly 6 x (one of each pattern, weighted 1, 4, 1).
// Two of those patterns are checked against published values in test_equity.cpp. The result is
// the commonly quoted AA vs KK figure of about 81.9% / 18.1% (as in CLAUDE.md).
TEST(RangeEquityExact, AcesVersusKingsIsTheSuitWeightedAverage) {
  const EquityResult range = exact(ranges({"AA", "KK"}));
  EXPECT_EQ(range.deals, 36u);
  EXPECT_EQ(range.boards, 36u * 1'712'304);
  const EquityResult bothSuits = exactKnown({"AsAh", "KsKh"});
  const EquityResult oneSuit = exactKnown({"AsAh", "KsKd"});
  const EquityResult noSuits = exactKnown({"AsAh", "KdKc"});
  expectSameCounts(weightedSum({{6, &bothSuits}, {24, &oneSuit}, {6, &noSuits}}), range);
  EXPECT_NEAR(range.equity(0), 0.819, 0.0005);
  EXPECT_NEAR(range.equity(1), 0.181, 0.0005);
}

// For each AKs hand, 3 QQ hands share its suit and 3 don't, so the range result is exactly
// 4 x 3 x (one of each). AsKs vs QhQd is checked against a published value in test_equity.cpp, and
// the range comes out at the commonly quoted 46% / 54% (as in CLAUDE.md).
TEST(RangeEquityExact, AceKingSuitedVersusQueensIsTheSuitWeightedAverage) {
  const EquityResult range = exact(ranges({"AKs", "QQ"}));
  EXPECT_EQ(range.deals, 24u);
  const EquityResult sharesSuit = exactKnown({"AsKs", "QsQh"});
  const EquityResult noSharedSuit = exactKnown({"AsKs", "QhQd"});
  expectSameCounts(weightedSum({{12, &sharesSuit}, {12, &noSharedSuit}}), range);
  EXPECT_NEAR(range.equity(0), 0.46, 0.005);
  EXPECT_NEAR(range.equity(1), 0.54, 0.005);
}

// The Wizard of Odds' "Two-Player Power Ratings in Texas Hold 'Em" (Michael Shackleford,
// https://wizardofodds.com/games/texas-hold-em/2-player-game/) enumerates every two-player deal
// exhaustively. For AA against one random hand it lists win 10,689,050,508, lose 1,827,970,020 and
// draw 68,413,872 outcomes, summed over the 6 AA hands. By suit symmetry each AA hand contributes
// exactly a sixth. 2.1 billion showdowns take about 10 s in Release on 16 threads, so this is
// opt-in:
//   poker_tests --gtest_also_run_disabled_tests --gtest_filter=*WizardOfOdds*
TEST(RangeEquityExact, DISABLED_AcesVersusRandomMatchesWizardOfOddsExactly) {
  const EquityResult r = exact(ranges({"AsAh", "random"}));
  EXPECT_EQ(r.deals, 1'225u);  // C(50,2) opponent hands
  EXPECT_EQ(6 * r.players[0].wins, 10'689'050'508u);
  EXPECT_EQ(6 * r.players[1].wins, 1'827'970'020u);
  EXPECT_EQ(6 * r.players[0].ties, 68'413'872u);
}

// ---- Exact: behaviour ---------------------------------------------------------------------------

TEST(RangeEquityExact, OneHandRangesMatchTheKnownHandVersion) {
  const auto known = std::get<EquityResult>(
      poker::exactEquity(holes({"AsKs", "QhQd", "7c7d"}), cards("Js Ts 2h")));
  const EquityResult range = exact(ranges({"AsKs", "QhQd", "7c7d"}), "Js Ts 2h");
  expectSameCounts(known, range);
  EXPECT_EQ(range.deals, 1u);
}

TEST(RangeEquityExact, HandsUsingBoardOrDeadCardsAreLeftOut) {
  // With the As on the board and the Ah dead, AA has a single hand left (AdAc), so it's the same
  // as that known hand.
  const EquityResult range = exact(ranges({"AA", "KhKd"}), "As 7c 2d", "Ah");
  const auto known = std::get<EquityResult>(
      poker::exactEquity(holes({"AdAc", "KhKd"}), cards("As 7c 2d"), cards("Ah")));
  EXPECT_EQ(range.deals, 1u);
  expectSameCounts(known, range);
}

TEST(RangeEquityExact, OverlappingRangesOnlyDealDisjointHands) {
  // AA against AA: each AA hand leaves exactly one AA hand for the opponent, so 6 deals, and
  // two pairs of aces always chop (unless the board makes a flush for one of them).
  const EquityResult r = exact(ranges({"AA", "AA"}), "Kc 7d 2h");
  EXPECT_EQ(r.deals, 6u);
  EXPECT_NEAR(r.equity(0), 0.5, 1e-12);
}

TEST(RangeEquityExact, ShowdownCountForChoosingAMethod) {
  const auto aaKk = ranges({"AA", "KK"});
  EXPECT_EQ(poker::exactShowdowns(aaKk, 0, 0, 1'000'000'000), 36u * 1'712'304);
  // With a small limit the count stops early, but still reports more than the limit.
  EXPECT_GT(poker::exactShowdowns(aaKk, 0, 0, 10'000'000), 10'000'000u);
  EXPECT_GT(poker::exactShowdowns(ranges({"random", "random"}), 0, 0, 1'000'000'000),
            1'000'000'000u);
  // Invalid input gives 0; exactEquity() reports the actual error.
  EXPECT_EQ(poker::exactShowdowns(ranges({"AsAh", "AsAh"}), 0, 0, 1'000'000'000), 0u);
}

// ---- Monte Carlo ------------------------------------------------------------------------------

// Against the Wizard of Odds' exhaustive table (see above): win, lose and draw for a hand against
// one random hand, rounded to two decimals.
TEST(RangeEquityMonteCarlo, HandsAgainstRandomMatchWizardOfOdds) {
  struct Case {
    const char* hand;
    double win, lose, draw;
  };
  for (const Case& c : {Case{"AA", 0.8493, 0.1452, 0.0054}, Case{"72o", 0.3171, 0.6254, 0.0575}}) {
    SCOPED_TRACE(c.hand);
    const MonteCarloResult r = monteCarlo(ranges({c.hand, "random"}), "", 300'000);
    expectNear(r.win(0), c.win, 0.00005);
    expectNear(r.win(1), c.lose, 0.00005);
    expectNear(r.tie(0), c.draw, 0.00005);
  }
}

TEST(RangeEquityMonteCarlo, AgreesWithExact) {
  const auto players = ranges({"QQ+, AKs", "JJ-99, AQs"});
  const EquityResult truth = exact(players, "Js Ts 2h");
  const MonteCarloResult mc = monteCarlo(players, "Js Ts 2h", 200'000);
  for (size_t i = 0; i < 2; ++i) {
    expectNear(mc.equity(i), truth.equity(i));
    expectNear(mc.win(i), truth.win(i));
  }
}

// Rejection sampling deals every valid combination of hands with equal probability; drawing each
// player's hand only from those that don't collide with the hands already drawn does not. Here
// player 1 holds AsAh or 4c6d, and AsAh collides with two of player 2's three hands. The 4 valid
// deals are (AsAh, KdKc), (4c6d, AsKs), (4c6d, AhQh) and (4c6d, KdKc), and on this river player 1
// wins only the first, so the true equity is exactly 1/4. The shortcut would deal AsAh half the
// time and get 1/2.
TEST(RangeEquityMonteCarlo, EveryValidDealIsEquallyLikely) {
  const auto players = ranges({"AsAh, 4c6d", "AsKs, AhQh, KdKc"});
  const EquityResult truth = exact(players, "2c 7d 9h Ts 3s");
  EXPECT_EQ(truth.deals, 4u);
  EXPECT_EQ(truth.equity(0), 0.25);
  expectNear(monteCarlo(players, "2c 7d 9h Ts 3s", 100'000).equity(0), 0.25);
}

TEST(RangeEquityMonteCarlo, OneHandRangesMatchTheKnownHandVersion) {
  const auto known = std::get<MonteCarloResult>(poker::monteCarloEquity(
      holes({"AsKs", "QhQd"}), cards("Js Ts 2h"), 0, {.trials = 100'000, .seed = 3}));
  const MonteCarloResult range = monteCarlo(ranges({"AsKs", "QhQd"}), "Js Ts 2h", 100'000, 3, 1);
  expectSameCounts(known.counts, range.counts);
  EXPECT_EQ(range.squaredPotUnits, known.squaredPotUnits);
}

TEST(RangeEquityMonteCarlo, SameResultForAnyThreadCountOrWayOfWritingTheRange) {
  const MonteCarloResult reference = monteCarlo(ranges({"QQ+, AKs", "random"}), "", 300'000, 9, 1);
  for (const unsigned threads : {2u, 8u}) {
    SCOPED_TRACE(threads);
    const MonteCarloResult r = monteCarlo(ranges({"QQ+, AKs", "random"}), "", 300'000, 9, threads);
    expectSameCounts(reference.counts, r.counts);
  }
  const MonteCarloResult reordered =
      monteCarlo(ranges({"AKs, AA, KK, QQ", "any"}), "", 300'000, 9, 1);
  expectSameCounts(reference.counts, reordered.counts);
}

// ---- Errors -----------------------------------------------------------------------------------

TEST(RangeEquityErrors, EmptyRangeAndImpossibleDeals) {
  const auto checkBoth = [](const std::vector<Range>& players, std::string_view board,
                            EquityError expected) {
    const auto e = poker::exactEquity(players, cards(board));
    const auto m = poker::monteCarloEquity(players, cards(board), 0, {.trials = 10, .seed = 1});
    ASSERT_TRUE(std::holds_alternative<EquityError>(e));
    ASSERT_TRUE(std::holds_alternative<EquityError>(m));
    EXPECT_EQ(std::get<EquityError>(e), expected);
    EXPECT_EQ(std::get<EquityError>(m), expected);
  };
  // Three aces on the board leave no AA hand at all.
  checkBoth(ranges({"AA", "KK"}), "As Ah Ad", EquityError::kEmptyRange);
  // Every hand in player 2's range needs a card player 1 holds.
  checkBoth(ranges({"AsAh", "AsKs, AhKh"}), "", EquityError::kNoValidDeal);
  // Three players can't all hold a pair of aces: there are only four aces.
  checkBoth(ranges({"AA", "AA", "AA"}), "", EquityError::kNoValidDeal);
}

// A one-hand range is a known hand, so its mistakes get the same error as the HoleCards version.
TEST(RangeEquityErrors, KnownHandsReportDuplicateCards) {
  for (const auto& [players, board] :
       {std::pair{ranges({"AsKs", "AsQd"}), ""}, std::pair{ranges({"AsKs", "QhQd"}), "As 2c 3d"}}) {
    const auto e = poker::exactEquity(players, cards(board));
    ASSERT_TRUE(std::holds_alternative<EquityError>(e));
    EXPECT_EQ(std::get<EquityError>(e), EquityError::kDuplicateCard);
    const auto m = poker::monteCarloEquity(players, cards(board), 0, {.trials = 10, .seed = 1});
    ASSERT_TRUE(std::holds_alternative<EquityError>(m));
    EXPECT_EQ(std::get<EquityError>(m), EquityError::kDuplicateCard);
  }
}

TEST(RangeEquityErrors, EveryErrorHasAMessage) {
  EXPECT_FALSE(poker::toString(EquityError::kEmptyRange).empty());
  EXPECT_FALSE(poker::toString(EquityError::kNoValidDeal).empty());
}

}  // namespace
