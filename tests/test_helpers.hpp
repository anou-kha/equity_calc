#pragma once

// Small helpers shared by the test files.

#include <bit>
#include <initializer_list>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

// Calls visit(hand) for every `size`-card subset of the deck, each exactly once.
template <typename Visit>
void forEachHand(int size, const Visit& visit, int firstCard = 0, CardMask hand = 0) {
  if (size == 0) {
    visit(hand);
    return;
  }
  for (int card = firstCard; card <= Card::kNumCards - size; ++card) {
    forEachHand(size - 1, visit, card + 1, hand | (CardMask{1} << card));
  }
}

// Parses one player's hole cards like "AsKs"; throws (failing the test) on malformed input.
inline HoleCards hole(std::string_view text) {
  const auto parsed = parseHoleCards(text);
  if (!parsed) throw std::invalid_argument("bad hole cards in test: " + std::string(text));
  return *parsed;
}

// Parses several players' hole cards, e.g. holes({"AsKs", "QhQd"}).
inline std::vector<HoleCards> holes(std::initializer_list<std::string_view> hands) {
  std::vector<HoleCards> players;
  for (std::string_view hand : hands) players.push_back(hole(hand));
  return players;
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
