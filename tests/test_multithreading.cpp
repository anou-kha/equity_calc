// Multithreaded equity must give exactly the same results as single-threaded equity, whatever the
// number of threads: the work is split into independent tasks and the totals are integers, so the
// order in which threads finish can't change anything. These tests check that bit for bit.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/monte_carlo.hpp"
#include "test_helpers.hpp"

namespace {

using poker::EquityResult;
using poker::HoleCards;
using poker::MonteCarloResult;
using poker::test::cards;
using poker::test::holes;

// The thread count to expect when asking for `requested` threads but only `maxUseful` have work.
unsigned expectedThreads(unsigned requested, unsigned maxUseful) {
  const unsigned wanted =
      requested != 0 ? requested : std::max(1u, std::thread::hardware_concurrency());
  return std::min(wanted, maxUseful);
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

MonteCarloResult monteCarlo(const std::vector<HoleCards>& players, std::string_view board,
                            uint64_t trials, unsigned threads) {
  const auto outcome = poker::monteCarloEquity(players, cards(board), 0,
                                               {.trials = trials, .seed = 7, .threads = threads});
  return std::get<MonteCarloResult>(outcome);
}

EquityResult exact(const std::vector<HoleCards>& players, std::string_view board,
                   std::string_view dead, unsigned threads) {
  const auto outcome = poker::exactEquity(players, cards(board), cards(dead), {.threads = threads});
  return std::get<EquityResult>(outcome);
}

// ---- Monte Carlo ------------------------------------------------------------------------------

TEST(MultithreadedMonteCarlo, ResultIsIdenticalForAnyThreadCount) {
  const auto players = holes({"AsKs", "QhQd", "7c7d"});
  // 300,000 trials = 4 full chunks of 65,536 plus a partial one, so up to 5 threads have work.
  const MonteCarloResult reference = monteCarlo(players, "Js Ts 2h", 300'000, 1);
  EXPECT_EQ(reference.counts.threads, 1u);
  for (const unsigned threads : {2u, 3u, 5u, 8u, 64u, 0u}) {
    SCOPED_TRACE(threads);
    const MonteCarloResult result = monteCarlo(players, "Js Ts 2h", 300'000, threads);
    EXPECT_EQ(result.counts.threads, expectedThreads(threads, 5));
    expectSameCounts(reference.counts, result.counts);
    EXPECT_EQ(result.squaredPotUnits, reference.squaredPotUnits);
  }
}

TEST(MultithreadedMonteCarlo, OneChunkUsesOneThread) {
  // 1,000 trials fit in a single chunk, so there's nothing to share and no extra thread starts.
  const auto players = holes({"AsKs", "QhQd"});
  const MonteCarloResult result = monteCarlo(players, "Js Ts 2h", 1'000, 8);
  EXPECT_EQ(result.counts.threads, 1u);
  expectSameCounts(monteCarlo(players, "Js Ts 2h", 1'000, 1).counts, result.counts);
}

// ---- Exact enumeration ------------------------------------------------------------------------

TEST(MultithreadedExact, ResultIsIdenticalForAnyThreadCount) {
  // AsAh vs KsKh preflop with 18 dead cards leaves 30 cards: C(30,5) = 142,506 boards. That's
  // enough work for 5 threads (each needs at least 25,000 boards) but quick even in Debug builds.
  const auto players = holes({"AsAh", "KsKh"});
  const std::string_view dead = "2c 2d 2h 2s 3c 3d 3h 3s 4c 4d 4h 4s 5c 5d 5h 5s 6c 6d";
  const EquityResult reference = exact(players, "", dead, 1);
  EXPECT_EQ(reference.boards, 142'506u);
  EXPECT_EQ(reference.threads, 1u);
  for (const unsigned threads : {2u, 3u, 5u, 8u, 0u}) {
    SCOPED_TRACE(threads);
    const EquityResult result = exact(players, "", dead, threads);
    EXPECT_EQ(result.threads, expectedThreads(threads, 5));
    expectSameCounts(reference, result);
  }
}

TEST(MultithreadedExact, SmallJobsStaySingleThreaded) {
  // A flop has only 990 boards to score: starting threads would cost more than the work itself.
  const auto players = holes({"AsKs", "QhQd"});
  const EquityResult result = exact(players, "Js Ts 2h", "", 8);
  EXPECT_EQ(result.threads, 1u);
  expectSameCounts(exact(players, "Js Ts 2h", "", 1), result);
}

TEST(MultithreadedExact, CompleteBoardIsOneTask) {
  const EquityResult result = exact(holes({"AhAd", "KcKd"}), "2c 7d 9h Ts 3s", "", 8);
  EXPECT_EQ(result.threads, 1u);
  EXPECT_EQ(result.boards, 1u);
  EXPECT_EQ(result.players[0].wins, 1u);
}

}  // namespace
