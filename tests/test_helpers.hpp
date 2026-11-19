#pragma once

// Small helpers shared by the test files.

#include <bit>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include "poker/card.hpp"
#include "poker/evaluator.hpp"

namespace poker {

// Let GoogleTest print hand ranks and categories readably in failure messages.
inline void PrintTo(HandRank rank, std::ostream* os) { *os << describe(rank); }
inline void PrintTo(HandCategory c, std::ostream* os) { *os << categoryName(c); }

namespace test {

// Parses cards like "As Kd 7h" into a mask; throws (failing the test) on malformed input.
inline CardMask cards(std::string_view text) {
  const auto parsed = parseCards(text);
  if (!parsed) throw std::invalid_argument("bad cards in test: " + std::string(text));
  return toMask(*parsed);
}

// A uniformly random hand of `size` distinct cards. It uses the generator's raw output rather than
// std::uniform_int_distribution, whose results differ between standard libraries, so every
// platform sees the same hands. (The modulo bias of 2^64 % 52 is negligible.)
inline CardMask randomHand(std::mt19937_64& rng, int size) {
  CardMask hand = 0;
  while (std::popcount(hand) < size) hand |= CardMask{1} << (rng() % Card::kNumCards);
  return hand;
}

}  // namespace test
}  // namespace poker
