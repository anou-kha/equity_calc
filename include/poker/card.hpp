#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace poker {

// A card is an index in [0, 52): index = rank * 4 + suit.
//   rank: 0 = '2', 1 = '3', ..., 8 = 'T', 9 = 'J', 10 = 'Q', 11 = 'K', 12 = 'A'
//   suit: 0 = 'c', 1 = 'd', 2 = 'h', 3 = 's'
class Card {
 public:
  static constexpr int kNumRanks = 13;
  static constexpr int kNumSuits = 4;
  static constexpr int kNumCards = 52;

  constexpr Card() = default;
  constexpr Card(int rank, int suit) : index_(static_cast<uint8_t>(rank * kNumSuits + suit)) {}
  static constexpr Card fromIndex(int index) {
    Card c;
    c.index_ = static_cast<uint8_t>(index);
    return c;
  }

  // Parses e.g. "As", "Td", "2c". Case-insensitive rank, lowercase or uppercase suit.
  static std::optional<Card> parse(std::string_view text);

  constexpr int index() const { return index_; }
  constexpr int rank() const { return index_ / kNumSuits; }
  constexpr int suit() const { return index_ % kNumSuits; }
  constexpr uint64_t mask() const { return uint64_t{1} << index_; }

  std::string toString() const;

  friend constexpr bool operator==(Card a, Card b) { return a.index_ == b.index_; }

 private:
  uint8_t index_ = 0;
};

// A set of cards stored as a 52-bit mask (bit i set => card with index i present).
using CardMask = uint64_t;

// Parses a run of cards with optional whitespace, e.g. "AsKs" or "Js Ts 2h".
// Returns std::nullopt on malformed input or duplicate cards.
std::optional<std::vector<Card>> parseCards(std::string_view text);

CardMask toMask(const std::vector<Card>& cards);

}  // namespace poker
