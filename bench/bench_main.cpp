// Benchmarks for the poker library: plain std::chrono, no external dependencies.
//
// Each benchmark runs one untimed warm-up pass, then several timed passes, and reports the fastest.
// The minimum is the measurement least disturbed by other processes and timer noise; the warm-up
// pass gets the data into the CPU caches first.
#include <bit>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/range.hpp"

namespace {

using poker::CardMask;

// A uniformly random hand of `size` distinct cards, built from the generator's raw output so the
// same seed gives the same hands with every standard library (std::uniform_int_distribution
// differs between them).
CardMask randomHand(std::mt19937_64& rng, int size) {
  CardMask hand = 0;
  while (std::popcount(hand) < size) hand |= CardMask{1} << (rng() % poker::Card::kNumCards);
  return hand;
}

// Runs `pass` once untimed, then `timedPasses` times, and returns the fastest pass in seconds.
template <typename Pass>
double fastestPassSeconds(int timedPasses, const Pass& pass) {
  using Clock = std::chrono::steady_clock;
  pass();
  double fastest = 0;
  for (int i = 0; i < timedPasses; ++i) {
    const auto start = Clock::now();
    pass();
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    if (i == 0 || seconds < fastest) fastest = seconds;
  }
  return fastest;
}

// The fast evaluator builds its lookup tables the first time it's called. Time that one call,
// before any other benchmark, so the build cost is visible and doesn't distort the hands/s figures.
void benchTableBuild() {
  const auto start = std::chrono::steady_clock::now();
  poker::buildEvaluatorTables();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::cout << "fast evaluator tables: built in " << std::fixed << std::setprecision(1)
            << seconds * 1000.0 << " ms on first use\n";
}

// Evaluates every hand once and returns the sum of the HandRank values. Using the results stops
// the compiler from optimising the work away, and equal sums show the evaluators agree.
template <typename Evaluate>
uint64_t sumOfRanks(const std::vector<CardMask>& hands, const Evaluate& evaluate) {
  uint64_t sum = 0;
  for (const CardMask hand : hands) sum += evaluate(hand).value();
  return sum;
}

// Both evaluators on the same seeded set of random 7-card hands.
void benchEvaluators() {
  constexpr int kHands = 1'000'000;
  constexpr int kTimedPasses = 5;
  constexpr uint64_t kSeed = 42;

  // Generate the hands up front so only evaluation is timed.
  std::mt19937_64 rng(kSeed);
  std::vector<CardMask> hands(kHands);
  for (CardMask& hand : hands) hand = randomHand(rng, 7);

  uint64_t naiveSum = 0;
  uint64_t fastSum = 0;
  const double naiveSeconds = fastestPassSeconds(kTimedPasses, [&] {
    naiveSum = sumOfRanks(hands, [](CardMask hand) { return poker::evaluateNaive(hand); });
  });
  const double fastSeconds = fastestPassSeconds(kTimedPasses, [&] {
    fastSum = sumOfRanks(hands, [](CardMask hand) { return poker::evaluate(hand); });
  });

  std::cout << std::fixed << std::setprecision(1)
            << "naive evaluator, 7-card hands: " << kHands / naiveSeconds / 1e6 << " M hands/s  ("
            << kHands << " hands, seed " << kSeed << ")\n"
            << "fast evaluator, 7-card hands:  " << kHands / fastSeconds / 1e6 << " M hands/s  ("
            << naiveSeconds / fastSeconds << "x the naive evaluator; checksums "
            << (naiveSum == fastSum ? "match" : "DIFFER") << ": " << fastSum << ")\n";
}

void benchExactEquityPreflop() {
  constexpr int kTimedPasses = 3;
  const std::vector<poker::HoleCards> players = {*poker::parseHoleCards("AsAh"),
                                                 *poker::parseHoleCards("KsKh")};

  uint64_t boards = 0;
  const double seconds = fastestPassSeconds(kTimedPasses, [&] {
    const auto outcome = poker::exactEquity(players, 0);
    boards = std::get<poker::EquityResult>(outcome).boards;
  });

  std::cout << "exact equity, AsAh vs KsKh preflop: " << std::fixed << std::setprecision(1)
            << seconds * 1000.0 << " ms  (" << boards << " boards, "
            << static_cast<double>(boards) / seconds / 1e6 << " M boards/s)\n";
}

// The same spot by Monte Carlo, to compare with exact enumeration: how long 1M random boards take,
// and how precise the answer is.
void benchMonteCarloPreflop() {
  constexpr int kTimedPasses = 3;
  constexpr uint64_t kTrials = 1'000'000;
  const std::vector<poker::HoleCards> players = {*poker::parseHoleCards("AsAh"),
                                                 *poker::parseHoleCards("KsKh")};

  poker::Estimate equity;
  const double seconds = fastestPassSeconds(kTimedPasses, [&] {
    const auto outcome = poker::monteCarloEquity(players, 0, 0, {.trials = kTrials, .seed = 1});
    equity = std::get<poker::MonteCarloResult>(outcome).equity(0);
  });

  std::cout << "monte carlo, AsAh vs KsKh preflop: " << std::fixed << std::setprecision(1)
            << seconds * 1000.0 << " ms  (" << kTrials << " trials, "
            << static_cast<double>(kTrials) / seconds / 1e6 << " M trials/s, AsAh equity "
            << std::setprecision(2) << 100.0 * equity.value << "% +/- " << 100.0 * equity.margin95()
            << "%)\n";
}

bool sameCounts(const poker::EquityResult& a, const poker::EquityResult& b) {
  if (a.boards != b.boards || a.players.size() != b.players.size()) return false;
  for (size_t i = 0; i < a.players.size(); ++i) {
    if (a.players[i].wins != b.players[i].wins || a.players[i].ties != b.players[i].ties ||
        a.players[i].tieShares != b.players[i].tieShares) {
      return false;
    }
  }
  return true;
}

// How exact enumeration and Monte Carlo speed up with more threads, and a check that every thread
// count gives exactly the same answer as one thread.
void benchThreadScaling() {
  constexpr int kTimedPasses = 2;
  constexpr uint64_t kTrials = 10'000'000;
  const std::vector<poker::HoleCards> players = {*poker::parseHoleCards("AsAh"),
                                                 *poker::parseHoleCards("KsKh")};

  std::cout << "thread scaling, AsAh vs KsKh preflop (" << std::thread::hardware_concurrency()
            << " hardware threads):\n"
            << "  threads   exact, 1,712,304 boards   monte carlo, 10M trials   same results\n";
  double exactBaseline = 0;
  double monteCarloBaseline = 0;
  poker::EquityResult exactReference;
  poker::EquityResult monteCarloReference;
  for (const unsigned threads : {1u, 2u, 4u, 8u, 16u}) {
    poker::EquityResult exact;
    poker::EquityResult monteCarlo;
    const double exactSeconds = fastestPassSeconds(kTimedPasses, [&] {
      exact =
          std::get<poker::EquityResult>(poker::exactEquity(players, 0, 0, {.threads = threads}));
    });
    const double monteCarloSeconds = fastestPassSeconds(kTimedPasses, [&] {
      monteCarlo = std::get<poker::MonteCarloResult>(
                       poker::monteCarloEquity(players, 0, 0,
                                               {.trials = kTrials, .seed = 1, .threads = threads}))
                       .counts;
    });
    if (threads == 1) {
      exactBaseline = exactSeconds;
      monteCarloBaseline = monteCarloSeconds;
      exactReference = exact;
      monteCarloReference = monteCarlo;
    }
    const bool same =
        sameCounts(exact, exactReference) && sameCounts(monteCarlo, monteCarloReference);
    std::cout << std::fixed << std::setprecision(1) << std::setw(9) << exact.threads
              << std::setw(12) << exactSeconds * 1000.0 << " ms " << std::setw(5)
              << exactBaseline / exactSeconds << "x" << std::setw(16) << monteCarloSeconds * 1000.0
              << " ms " << std::setw(5) << monteCarloBaseline / monteCarloSeconds << "x"
              << std::setw(15) << (same ? "yes" : "NO") << '\n';
  }
}

poker::Range rangeOf(std::string_view text) {
  return std::get<poker::Range>(poker::parseRange(text));
}

// Range against range, on all hardware threads: an exact spot (AA vs KK: 36 deals of the hands x
// 1,712,304 boards) and a Monte Carlo spot too big to enumerate (QQ+, AKs against any two cards).
void benchRanges() {
  constexpr int kTimedPasses = 2;
  constexpr uint64_t kTrials = 10'000'000;

  const std::vector<poker::Range> aaKk = {rangeOf("AA"), rangeOf("KK")};
  poker::EquityResult exact;
  const double exactSeconds = fastestPassSeconds(kTimedPasses, [&] {
    exact = std::get<poker::EquityResult>(poker::exactEquity(aaKk, 0, 0, {.threads = 0}));
  });
  std::cout << std::fixed << std::setprecision(1)
            << "ranges, exact AA vs KK: " << exactSeconds * 1000.0 << " ms  (" << exact.boards
            << " showdowns, " << static_cast<double>(exact.boards) / exactSeconds / 1e6
            << " M/s on " << exact.threads << " threads, AA equity " << std::setprecision(2)
            << 100.0 * exact.equity(0) << "%)\n";

  const std::vector<poker::Range> wide = {rangeOf("QQ+, AKs"), rangeOf("random")};
  poker::Estimate equity;
  unsigned threads = 0;
  const double monteCarloSeconds = fastestPassSeconds(kTimedPasses, [&] {
    const auto result = std::get<poker::MonteCarloResult>(
        poker::monteCarloEquity(wide, 0, 0, {.trials = kTrials, .seed = 1, .threads = 0}));
    equity = result.equity(0);
    threads = result.counts.threads;
  });
  std::cout << std::fixed << std::setprecision(1)
            << "ranges, monte carlo QQ+,AKs vs random: " << monteCarloSeconds * 1000.0 << " ms  ("
            << kTrials << " trials on " << threads << " threads, equity " << std::setprecision(2)
            << 100.0 * equity.value << "% +/- " << 100.0 * equity.margin95() << "%)\n";
}

}  // namespace

int main() {
#ifndef NDEBUG
  std::cout << "warning: this is not a Release build, so the numbers below are not meaningful\n";
#endif
  benchTableBuild();
  benchEvaluators();
  benchExactEquityPreflop();
  benchMonteCarloPreflop();
  benchThreadScaling();
  benchRanges();
  return 0;
}
