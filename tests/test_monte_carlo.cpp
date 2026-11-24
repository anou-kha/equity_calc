#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/monte_carlo.hpp"
#include "test_helpers.hpp"

namespace {

using poker::EquityError;
using poker::EquityResult;
using poker::Estimate;
using poker::HoleCards;
using poker::kPotUnits;
using poker::MonteCarloResult;
using poker::test::cards;
using poker::test::hole;
using poker::test::holes;

// Runs monteCarloEquity on valid input; an error fails the test.
MonteCarloResult monteCarlo(const std::vector<HoleCards>& players, std::string_view board,
                            uint64_t trials, uint64_t seed, std::string_view dead = "") {
  const auto outcome =
      poker::monteCarloEquity(players, cards(board), cards(dead), {.trials = trials, .seed = seed});
  if (const auto* error = std::get_if<EquityError>(&outcome)) {
    throw std::runtime_error("unexpected error: " + std::string(poker::toString(*error)));
  }
  return std::get<MonteCarloResult>(outcome);
}

EquityResult exact(const std::vector<HoleCards>& players, std::string_view board) {
  return std::get<EquityResult>(poker::exactEquity(players, cards(board)));
}

// Every trial's pot goes to somebody, just as for exact enumeration.
void expectPotFullyDistributed(const MonteCarloResult& result) {
  uint64_t units = 0;
  for (const poker::PlayerResult& p : result.counts.players) {
    units += p.wins * kPotUnits + p.tieShares;
  }
  EXPECT_EQ(units, result.counts.boards * kPotUnits);
}

// ---- Reproducibility --------------------------------------------------------------------------

TEST(MonteCarlo, SameSeedGivesIdenticalResults) {
  const auto players = holes({"AsKs", "QhQd", "7c7d"});
  // 100,000 trials spans two chunks of random numbers.
  const MonteCarloResult a = monteCarlo(players, "Js Ts 2h", 100'000, 7);
  const MonteCarloResult b = monteCarlo(players, "Js Ts 2h", 100'000, 7);
  for (size_t i = 0; i < players.size(); ++i) {
    EXPECT_EQ(a.counts.players[i].wins, b.counts.players[i].wins);
    EXPECT_EQ(a.counts.players[i].ties, b.counts.players[i].ties);
    EXPECT_EQ(a.counts.players[i].tieShares, b.counts.players[i].tieShares);
    EXPECT_EQ(a.squaredPotUnits[i], b.squaredPotUnits[i]);
  }
  // A different seed deals different boards.
  const MonteCarloResult c = monteCarlo(players, "Js Ts 2h", 100'000, 8);
  EXPECT_NE(a.counts.players[0].wins, c.counts.players[0].wins);
}

// ---- Agreement with exact enumeration ---------------------------------------------------------

struct Spot {
  const char* name;  // becomes part of the test name
  std::vector<std::string> hands;
  const char* board;
  uint64_t trials;
};

void PrintTo(const Spot& spot, std::ostream* os) { *os << spot.name; }

const Spot kSpots[] = {
    {"HeadsUpPreflop", {"AsKs", "QhQd"}, "", 200'000},
    {"ThreeWayFlop", {"AsKs", "QhQd", "7c7d"}, "Js Ts 2h", 200'000},
    {"FourWayFlopWithManyTies", {"2c3d", "2d3c", "2h3s", "2s3h"}, "Ac Kd 7h", 100'000},
    {"HeadsUpTurn", {"AsKs", "QhQd"}, "Js Ts 2h 3c", 100'000},
    {"NineWayFlop",
     {"2c2d", "3c3d", "4c4d", "5c5d", "6c6d", "7c7d", "8c8d", "9c9d", "TcTd"},
     "Ah Kh Qh",
     100'000},
};

class MonteCarloAgreement : public testing::TestWithParam<Spot> {};

// Every estimate should be within 4 standard errors of the exact answer. The fixed seed makes the
// test deterministic; the 4-sigma margin means a correct implementation would pass for nearly any
// seed (a single estimate misses by more than that only about 1 time in 16,000).
TEST_P(MonteCarloAgreement, EstimatesAreWithinFourStandardErrorsOfExact) {
  const Spot& spot = GetParam();
  std::vector<HoleCards> players;
  for (const std::string& hand : spot.hands) players.push_back(hole(hand));

  const EquityResult truth = exact(players, spot.board);
  const MonteCarloResult mc = monteCarlo(players, spot.board, spot.trials, 2026);
  EXPECT_EQ(mc.counts.boards, spot.trials);
  expectPotFullyDistributed(mc);
  for (size_t i = 0; i < players.size(); ++i) {
    const Estimate equity = mc.equity(i);
    const Estimate win = mc.win(i);
    EXPECT_LE(std::abs(equity.value - truth.equity(i)), 4 * equity.standardError)
        << "player " << i << " equity " << equity.value << " vs exact " << truth.equity(i);
    EXPECT_LE(std::abs(win.value - truth.win(i)), 4 * win.standardError)
        << "player " << i << " win " << win.value << " vs exact " << truth.win(i);
  }
}

INSTANTIATE_TEST_SUITE_P(Spots, MonteCarloAgreement, testing::ValuesIn(kSpots),
                         [](const testing::TestParamInfo<Spot>& info) {
                           return std::string(info.param.name);
                         });

// ---- Confidence intervals ---------------------------------------------------------------------

// A 95% confidence interval should contain the true value in about 95% of independent runs. This
// checks the standard error formula itself, not just one estimate.
TEST(MonteCarloConfidence, NinetyFivePercentIntervalsCoverTheTrueValue) {
  const auto players = holes({"AsKs", "QhQd"});
  const double truth = exact(players, "Js Ts 2h").equity(0);
  constexpr int kRuns = 400;
  int covered = 0;
  for (uint64_t seed = 0; seed < kRuns; ++seed) {
    const Estimate e = monteCarlo(players, "Js Ts 2h", 1'000, seed).equity(0);
    if (std::abs(e.value - truth) <= e.margin95()) ++covered;
  }
  // Expect about 380 of 400; the count itself varies by about 4.4 runs from seed set to seed set.
  const double coverage = covered / static_cast<double>(kRuns);
  EXPECT_GE(coverage, 0.92);
  EXPECT_LE(coverage, 0.98);
}

TEST(MonteCarloEstimate, StandardErrorFromSums) {
  // Four trials: win, win, lose, lose. Their sample variance is 1/3, so the standard error of the
  // mean is sqrt((1/3) / 4) = sqrt(1/12).
  MonteCarloResult r;
  r.counts.boards = 4;
  r.counts.players.resize(1);
  r.counts.players[0].wins = 2;
  r.squaredPotUnits = {2 * kPotUnits * kPotUnits};
  EXPECT_DOUBLE_EQ(r.win(0).value, 0.5);
  EXPECT_DOUBLE_EQ(r.win(0).standardError, std::sqrt(1.0 / 12.0));
  EXPECT_DOUBLE_EQ(r.win(0).margin95(), 1.96 * std::sqrt(1.0 / 12.0));
  EXPECT_DOUBLE_EQ(r.equity(0).value, 0.5);
  EXPECT_DOUBLE_EQ(r.equity(0).standardError, std::sqrt(1.0 / 12.0));
  EXPECT_DOUBLE_EQ(r.tie(0).value, 0.0);
  EXPECT_DOUBLE_EQ(r.tie(0).standardError, 0.0);
}

TEST(MonteCarloEstimate, OneTrialHasUnknownSpread) {
  const MonteCarloResult r = monteCarlo(holes({"AsKs", "QhQd"}), "Js Ts 2h", 1, 1);
  EXPECT_TRUE(std::isinf(r.equity(0).standardError));
}

// ---- Exact situations -------------------------------------------------------------------------

TEST(MonteCarloExact, RiverHasNoUncertainty) {
  // A nine-high straight on the board: every trial is the same 3-way chop.
  const MonteCarloResult r =
      monteCarlo(holes({"AhAd", "KcKd", "2h2s"}), "5c 6d 7h 8s 9c", 1'000, 1);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(r.equity(i).value, 1.0 / 3.0);
    EXPECT_EQ(r.equity(i).standardError, 0.0);
  }
}

TEST(MonteCarloExact, DeadCardsAreNeverDealt) {
  // Kings' set vs queens on Kc 7s 2d: the queens' only out is Qh + Qd. With the Qh dead, a correct
  // simulation can never deal it, so the kings win every trial.
  const MonteCarloResult r = monteCarlo(holes({"KhKd", "QcQs"}), "Kc 7s 2d", 50'000, 1, "Qh");
  EXPECT_EQ(r.counts.players[0].wins, 50'000u);
  EXPECT_EQ(r.equity(1).value, 0.0);
}

TEST(MonteCarloExact, TrialCountIsExactAcrossChunks) {
  const MonteCarloResult r = monteCarlo(holes({"AsKs", "QhQd"}), "Js Ts 2h", 65'537, 1);
  EXPECT_EQ(r.counts.boards, 65'537u);  // one full chunk of 65,536 plus one more trial
  expectPotFullyDistributed(r);
}

// ---- Validation -------------------------------------------------------------------------------

TEST(MonteCarloValidation, Errors) {
  const auto players = holes({"AsKs", "QhQd"});
  const auto noTrials = poker::monteCarloEquity(players, 0, 0, {.trials = 0, .seed = 1});
  ASSERT_TRUE(std::holds_alternative<EquityError>(noTrials));
  EXPECT_EQ(std::get<EquityError>(noTrials), EquityError::kNoTrials);

  // The same input checks as exactEquity apply.
  const auto duplicate =
      poker::monteCarloEquity(holes({"AsKs", "AsQd"}), 0, 0, {.trials = 10, .seed = 1});
  ASSERT_TRUE(std::holds_alternative<EquityError>(duplicate));
  EXPECT_EQ(std::get<EquityError>(duplicate), EquityError::kDuplicateCard);
}

}  // namespace
