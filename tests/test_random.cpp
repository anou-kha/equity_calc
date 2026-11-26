#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

#include "poker/random.hpp"

namespace {

using poker::mixSeed;
using poker::Rng;
using poker::uniformBelow;

TEST(UniformBelow, StaysInRange) {
  Rng rng(1);
  for (const uint64_t n : {uint64_t{1}, uint64_t{2}, uint64_t{3}, uint64_t{52}, uint64_t{1000},
                           (uint64_t{1} << 63) + 1, std::numeric_limits<uint64_t>::max()}) {
    for (int i = 0; i < 10'000; ++i) ASSERT_LT(uniformBelow(rng, n), n) << "n = " << n;
  }
}

TEST(UniformBelow, MatchesSeededRejectionSequence) {
  Rng rng(42);
  Rng reference(42);
  // The large bounds exercise both rejected draws and accepted draws below n.
  for (const uint64_t n : {uint64_t{1}, uint64_t{52}, (uint64_t{1} << 63) + 1, uint64_t{3} << 62,
                           std::numeric_limits<uint64_t>::max()}) {
    const uint64_t threshold = (uint64_t{0} - n) % n;
    for (int i = 0; i < 1000; ++i) {
      uint64_t raw = reference();
      while (raw < threshold) raw = reference();
      ASSERT_EQ(uniformBelow(rng, n), raw % n) << "n = " << n << ", draw " << i;
    }
  }
  EXPECT_EQ(rng(), reference());
}

// Pearson's chi-squared statistic: the sum of (observed - expected)^2 / expected over all values.
// For truly uniform draws it averages (number of values - 1).
double chiSquared(const std::vector<uint64_t>& counts, uint64_t draws) {
  const double expected = static_cast<double>(draws) / static_cast<double>(counts.size());
  double statistic = 0;
  for (const uint64_t count : counts) {
    const double difference = static_cast<double>(count) - expected;
    statistic += difference * difference / expected;
  }
  return statistic;
}

TEST(UniformBelow, IsUniformForSmallRanges) {
  Rng rng(2);
  for (const uint64_t n : {uint64_t{3}, uint64_t{52}}) {
    std::vector<uint64_t> counts(n);
    const uint64_t draws = 10'000 * n;
    for (uint64_t i = 0; i < draws; ++i) ++counts[uniformBelow(rng, n)];
    // The statistic has mean n - 1 and standard deviation sqrt(2(n - 1)); allow 5 deviations.
    const auto degreesOfFreedom = static_cast<double>(n - 1);
    EXPECT_LT(chiSquared(counts, draws), degreesOfFreedom + 5 * std::sqrt(2 * degreesOfFreedom))
        << "n = " << n;
  }
}

// With n = 3 * 2^62, the 2^64 raw values are one whole block of n plus 2^62 left over. Plain
// `rng() % n` maps those leftovers onto [0, 2^62) again, so the lowest third of the results comes
// up half the time instead of a third of the time. Rejection sampling must not have that bias.
TEST(UniformBelow, HasNoModuloBias) {
  constexpr uint64_t n = uint64_t{3} << 62;
  constexpr int kDraws = 30'000;
  Rng rng(3);
  int lowThird = 0;
  int lowThirdWithPlainModulo = 0;
  for (int i = 0; i < kDraws; ++i) {
    if (uniformBelow(rng, n) < n / 3) ++lowThird;
    if (rng() % n < n / 3) ++lowThirdWithPlainModulo;
  }
  // The standard deviation of these fractions is under 0.003, so 0.015 is a wide margin.
  EXPECT_NEAR(lowThird / static_cast<double>(kDraws), 1.0 / 3.0, 0.015);
  // The same check does catch plain modulo, so the test really can see the bias.
  EXPECT_NEAR(lowThirdWithPlainModulo / static_cast<double>(kDraws), 0.5, 0.015);
}

// SplitMix64's well-known first outputs for seed 0, which I also recomputed independently with
// big-integer arithmetic (PowerShell's System.Numerics.BigInteger).
TEST(MixSeed, MatchesSplitMix64) {
  EXPECT_EQ(mixSeed(0, 0), 0xE220A8397B1DCDAFu);
  EXPECT_EQ(mixSeed(0, 1), 0x6E789E6AA1B965F4u);
  EXPECT_EQ(mixSeed(0, 2), 0x06C45D188009454Fu);
  EXPECT_EQ(mixSeed(42, 0), 0xBDD732262FEB6E95u);
}

TEST(MixSeed, NearbyInputsGiveDistinctSeeds) {
  std::unordered_set<uint64_t> seeds;
  for (uint64_t i = 0; i < 10'000; ++i) {
    seeds.insert(mixSeed(7, i));  // consecutive streams
    seeds.insert(mixSeed(i, 0));  // consecutive base seeds
  }
  // SplitMix64's mixing is a bijection, so outputs only collide when the internal states are equal.
  // That happens exactly once here: both loops produce mixSeed(7, 0) (i = 0 in the first, i = 7 in
  // the second). All the other 19,998 values must be distinct.
  EXPECT_EQ(seeds.size(), 19'999u);
}

}  // namespace
