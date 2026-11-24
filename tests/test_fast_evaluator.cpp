// Tests specific to the lookup-table evaluator: it must agree with the naive reference on every
// hand, its tables must be exactly as large as needed, and building them must be thread-safe.
// (The behaviour tests in test_evaluator.cpp also run against both evaluators.)
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "poker/card.hpp"
#include "poker/evaluator.hpp"
#include "test_helpers.hpp"

namespace {

using poker::CardMask;
using poker::evaluate;
using poker::evaluateNaive;
using poker::test::forEachHand;
using poker::test::randomHand;

// Compares the two evaluators on every hand of `size` cards and returns the number of mismatches.
int64_t mismatchesOnAllHands(int size) {
  int64_t mismatches = 0;
  forEachHand(size, [&](CardMask hand) {
    if (evaluate(hand) != evaluateNaive(hand)) ++mismatches;
  });
  return mismatches;
}

// The perfect hash is minimal: one table entry for each way to hold k cards with at most 4 of a
// rank. Choosing k ranks from 13 with repetition gives C(12 + k, k) ways; subtract those with 5 or
// more of one rank (13 choices of that rank, times C(7 + k, k - 5) ways to choose the others).
TEST(FastEvaluator, NonFlushTablesHaveOneEntryPerRankCountVector) {
  EXPECT_EQ(poker::detail::nonFlushTableSize(5), 6'175u);   // C(17,5) - 13 * 1
  EXPECT_EQ(poker::detail::nonFlushTableSize(6), 18'395u);  // C(18,6) - 13 * 13
  EXPECT_EQ(poker::detail::nonFlushTableSize(7), 49'205u);  // C(19,7) - 13 * 91
}

TEST(FastEvaluator, MatchesNaiveOnAllFiveCardHands) { EXPECT_EQ(mismatchesOnAllHands(5), 0); }

TEST(FastEvaluator, MatchesNaiveOnRandomSixAndSevenCardHands) {
  std::mt19937_64 rng(5);
  for (int i = 0; i < 500'000; ++i) {
    const CardMask hand = randomHand(rng, i % 4 == 0 ? 6 : 7);
    ASSERT_EQ(evaluate(hand), evaluateNaive(hand)) << "hand mask " << hand;
  }
}

// All 20,358,520 six-card and 133,784,560 seven-card hands. These take a few seconds in Release
// and many minutes in Debug, so they are disabled by default. Run them with:
//   poker_tests --gtest_also_run_disabled_tests --gtest_filter=FastEvaluator.*
TEST(FastEvaluator, DISABLED_MatchesNaiveOnAllSixCardHands) {
  EXPECT_EQ(mismatchesOnAllHands(6), 0);
}

TEST(FastEvaluator, DISABLED_MatchesNaiveOnAllSevenCardHands) {
  EXPECT_EQ(mismatchesOnAllHands(7), 0);
}

TEST(FastEvaluator, BuildingTablesEarlyIsHarmless) {
  poker::buildEvaluatorTables();
  poker::buildEvaluatorTables();  // a second call does nothing
  std::mt19937_64 rng(6);
  for (int i = 0; i < 1'000; ++i) {
    const CardMask hand = randomHand(rng, 7);
    ASSERT_EQ(evaluate(hand), evaluateNaive(hand)) << "hand mask " << hand;
  }
}

// The tables are built on first use. ctest runs each test in a fresh process, so here several
// threads really do race to make that first call. C++ guarantees the tables are built exactly once
// and that every thread sees them complete, so every answer must still match the reference.
TEST(FastEvaluator, ConcurrentFirstUseIsSafe) {
  constexpr int kThreads = 8;
  std::atomic<bool> go = false;
  std::atomic<int> mismatches = 0;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t, &go, &mismatches] {
      while (!go) std::this_thread::yield();  // start all threads at (nearly) the same moment
      std::mt19937_64 rng(static_cast<uint64_t>(t));
      for (int i = 0; i < 20'000; ++i) {
        const CardMask hand = randomHand(rng, 7);
        if (evaluate(hand) != evaluateNaive(hand)) ++mismatches;
      }
    });
  }
  go = true;
  for (std::thread& thread : threads) thread.join();
  EXPECT_EQ(mismatches, 0);
}

}  // namespace
