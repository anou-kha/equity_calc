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
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"
#include "poker/monte_carlo.hpp"

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

void benchNaiveEvaluator() {
  constexpr int kHands = 1'000'000;
  constexpr int kTimedPasses = 5;
  constexpr uint64_t kSeed = 42;

  // Generate the hands up front so only evaluation is timed.
  std::mt19937_64 rng(kSeed);
  std::vector<CardMask> hands(kHands);
  for (CardMask& hand : hands) hand = randomHand(rng, 7);

  // Summing the results stops the compiler from optimising the evaluation away.
  uint64_t checksum = 0;
  const double seconds = fastestPassSeconds(kTimedPasses, [&] {
    for (CardMask hand : hands) checksum += poker::evaluate(hand).value();
  });

  std::cout << "naive evaluator, 7-card hands: " << std::fixed << std::setprecision(1)
            << kHands / seconds / 1e6 << " M hands/s  (" << kHands << " hands, seed " << kSeed
            << ", checksum " << checksum << ")\n";
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

}  // namespace

int main() {
#ifndef NDEBUG
  std::cout << "warning: this is not a Release build, so the numbers below are not meaningful\n";
#endif
  benchNaiveEvaluator();
  benchExactEquityPreflop();
  benchMonteCarloPreflop();
  return 0;
}
