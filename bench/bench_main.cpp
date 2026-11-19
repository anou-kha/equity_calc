// Minimal benchmark harness (no external dependency).
// Replace / extend with hand-evaluator and equity benchmarks as they land.
#include <chrono>
#include <cstdint>
#include <iostream>

#include "poker/card.hpp"

int main() {
  using clock = std::chrono::steady_clock;
  constexpr int kIters = 10'000'000;

  uint64_t sink = 0;
  const auto start = clock::now();
  for (int i = 0; i < kIters; ++i) {
    sink += poker::Card::fromIndex(i % poker::Card::kNumCards).mask();
  }
  const auto end = clock::now();

  const double secs = std::chrono::duration<double>(end - start).count();
  std::cout << "card mask: " << static_cast<double>(kIters) / secs / 1e6 << " M ops/sec"
            << " (sink=" << sink << ")\n";
  return 0;
}
