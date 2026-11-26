// The heads-up fast path (detail::addHeadsUpShowdown) must give exactly the same counts, tie shares
// and Monte Carlo squared pot units as the general showdown(), which stays the reference. The
// seeded range test's numbers come from the general scorer: this file also passes on the code
// from before the fast path existed.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "../src/showdown.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/random.hpp"
#include "test_helpers.hpp"

namespace poker {
namespace {

using test::cards;
using test::holes;

void expectRiverMatchesGeneral(const std::vector<HoleCards>& players, CardMask board) {
  SCOPED_TRACE(board);
  const std::array masks{players[0][0].mask() | players[0][1].mask(),
                         players[1][0].mask() | players[1][1].mask()};
  const auto shares = detail::showdown(board, masks);
  std::array<PlayerResult, 2> expected{};
  detail::addToCounts(shares, expected);

  const auto exact = std::get<EquityResult>(exactEquity(players, board));
  constexpr uint64_t trials = 3;
  const auto mc = std::get<MonteCarloResult>(
      monteCarloEquity(players, board, 0, {.trials = trials, .seed = 13}));
  ASSERT_EQ(exact.boards, 1u);
  ASSERT_EQ(mc.counts.boards, trials);
  for (size_t i = 0; i < expected.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(exact.players[i].wins, expected[i].wins);
    EXPECT_EQ(exact.players[i].ties, expected[i].ties);
    EXPECT_EQ(exact.players[i].tieShares, expected[i].tieShares);
    EXPECT_EQ(mc.counts.players[i].wins, trials * expected[i].wins);
    EXPECT_EQ(mc.counts.players[i].ties, trials * expected[i].ties);
    EXPECT_EQ(mc.counts.players[i].tieShares, trials * expected[i].tieShares);
    EXPECT_EQ(mc.squaredPotUnits[i], trials * shares[i] * shares[i]);
  }
}

TEST(HeadsUpShowdown, MatchesGeneralOnSeededRiverBoards) {
  const auto players = holes({"AsKs", "QhQd"});
  const CardMask used = cards("As Ks Qh Qd");
  Rng rng(42);
  for (int i = 0; i < 4096; ++i) {
    CardMask board;
    do {
      board = test::randomHand(rng, 5);
    } while (board & used);
    expectRiverMatchesGeneral(players, board);
  }
}

TEST(HeadsUpShowdown, MatchesGeneralWhenBothHandsMakeBroadwayOrTheBoardPlays) {
  const auto players = holes({"AsKd", "AdKs"});
  expectRiverMatchesGeneral(players, cards("Qc Jh Ts 3s 2d"));
  expectRiverMatchesGeneral(players, cards("Th Jh Qh Kh Ah"));
}

TEST(HeadsUpShowdown, SeededRangeSamplingMatchesGeneralAcrossChunks) {
  // Reference counts with the general scorer for two full chunks and a one-trial tail.
  const std::array players{std::get<Range>(parseRange("QQ+, AKs")),
                           std::get<Range>(parseRange("random"))};
  for (const unsigned threads : {1u, 3u}) {
    SCOPED_TRACE(threads);
    const auto mc = std::get<MonteCarloResult>(
        monteCarloEquity(players, 0, 0, {.trials = 131073, .seed = 17, .threads = threads}));
    ASSERT_EQ(mc.counts.boards, 131073u);
    EXPECT_EQ(mc.counts.players[0].wins, 103734u);
    EXPECT_EQ(mc.counts.players[1].wins, 26273u);
    for (const auto& player : mc.counts.players) {
      EXPECT_EQ(player.ties, 1066u);
      EXPECT_EQ(player.tieShares, 1'343'160u);
    }
    EXPECT_EQ(mc.squaredPotUnits[0], 660'444'775'200ull);
    EXPECT_EQ(mc.squaredPotUnits[1], 168'536'440'800ull);
  }
}

}  // namespace
}  // namespace poker
