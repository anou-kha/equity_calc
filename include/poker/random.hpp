#pragma once

#include <cstdint>
#include <random>

namespace poker {

// The random number generator used for Monte Carlo. The C++ standard specifies std::mt19937_64's
// output bit for bit, so a given seed produces the same numbers with every compiler and standard
// library. (The standard distributions, such as std::uniform_int_distribution, are not specified
// that precisely, which is why this file provides its own uniformBelow().)
using Rng = std::mt19937_64;

// A uniformly random integer in [0, n), for n > 0, with no bias.
//
// `rng() % n` is very slightly biased whenever 2^64 isn't a multiple of n: the raw values form
// whole blocks of n plus one incomplete block, so the smallest results get one extra chance.
// Rejection sampling throws away raw values from that incomplete block and draws again.
uint64_t uniformBelow(Rng& rng, uint64_t n);

// Turns a seed and a stream number into a well-mixed 64-bit seed: the (stream + 1)-th output of a
// SplitMix64 generator started at `seed`. Nearby inputs (stream 0, 1, 2, ...) give unrelated
// results, so each stream can seed its own independent Rng.
uint64_t mixSeed(uint64_t seed, uint64_t stream);

}  // namespace poker
