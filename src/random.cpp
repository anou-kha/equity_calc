#include "poker/random.hpp"

#include <cassert>
#include <limits>

namespace poker {

uint64_t uniformBelow(Rng& rng, uint64_t n) {
  assert(n > 0);
  // Plain raw % n would be biased: 2^64 isn't a multiple of n, so results below 2^64 mod n would
  // come up slightly more often than the others. So the first 2^64 mod n raw values (the
  // "incomplete block") are rejected and redrawn. The remaining 2^64 - incomplete values are an
  // exact multiple of n, so every result is equally likely. For small n (like 52) a rejection
  // almost never happens.
  uint64_t raw = rng();
  // The incomplete block is always smaller than n, so a draw >= n (almost every draw) can't be in
  // it, and the division that measures the block can be skipped.
  if (raw < n) {
    // 2^64 itself doesn't fit in 64 bits, so compute (2^64 - n) mod n, which is the same number.
    const uint64_t incomplete = (std::numeric_limits<uint64_t>::max() - n + 1) % n;
    while (raw < incomplete) raw = rng();
  }
  return raw % n;
}

uint64_t mixSeed(uint64_t seed, uint64_t stream) {
  // SplitMix64 (Steele, Lea & Flood, 2014): step the state by the golden-ratio constant, then
  // scramble it with two xor-shift-multiply rounds, so every input bit affects every output bit.
  uint64_t z = seed + (stream + 1) * 0x9E3779B97F4A7C15u;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
  return z ^ (z >> 31);
}

}  // namespace poker
