#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include "poker/card.hpp"

namespace poker {

// Hand categories from weakest to strongest. HandRank stores the category in its top bits, so this
// order is what makes a flush beat a straight: don't reorder.
enum class HandCategory : uint8_t {
  kHighCard,
  kPair,
  kTwoPair,
  kThreeOfAKind,
  kStraight,
  kFlush,
  kFullHouse,
  kFourOfAKind,
  kStraightFlush,
};

inline constexpr int kNumHandCategories = 9;

// The strength of the best five-card hand in a set of cards. A higher HandRank beats a lower one;
// equal HandRanks split the pot.
//
// Encoding: the category sits in bits 20..23, followed by up to five 4-bit card ranks
// (0 = '2' ... 12 = 'A') in bits 16..19, 12..15, ..., 0..3, most important rank first; unused
// slots are zero. Comparing two values as integers therefore compares category first, then the
// ranks that decide ties within a category, like comparing words letter by letter.
// For example, "Full house, Kings full of Sevens" is (kFullHouse, K, 7, 0, 0, 0).
//
// A default-constructed HandRank (value 0) is lower than every real hand.
class HandRank {
 public:
  constexpr HandRank() = default;
  constexpr explicit HandRank(uint32_t value) : value_(value) {}

  constexpr uint32_t value() const { return value_; }

  auto operator<=>(const HandRank&) const = default;

 private:
  uint32_t value_ = 0;
};

// Evaluates the best five-card hand that can be made from `cards`, which must hold 5, 6 or 7
// cards. This is the simple reference implementation: it is written to be obviously correct, and
// faster evaluators are tested against it.
HandRank evaluate(CardMask cards);

HandCategory category(HandRank rank);

// "High card", "Pair", ..., "Straight flush".
std::string_view categoryName(HandCategory c);

// Human-readable description, e.g. "Two pair, Aces and Kings (Queen kicker)" or "Royal flush".
std::string describe(HandRank rank);

}  // namespace poker
