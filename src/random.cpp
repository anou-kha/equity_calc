#include "poker/random.hpp"

#include <cassert>
#include <limits>

namespace poker {

uint64_t uniformBelow(Rng& rng, uint64_t n) {
  assert(n > 0);
  uint64_t raw = rng();
  // The rejection threshold is 2^64 mod n, always below n. A draw >= n skips that division.
  if (raw < n) {
    // (2^64 - n) mod n gives the same threshold and fits in 64 bits.
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
