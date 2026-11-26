#pragma once

// The 169 starting-hand classes ranked by how well they do against one random hand, and "top X%"
// ranges built from that ranking (the range picker's slider on the web page).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "poker/range.hpp"

namespace poker {

// A starting-hand class (a pair like "AA", or a suited or offsuit hand like "AKs" or "AKo") and its
// exact preflop result against one random hand: one hand of the class (by suit symmetry, any one)
// against each of the 1,225 hands it can meet, on all 1,712,304 boards. tools/rank_hands.cpp
// computed these with exactEquity().
struct RankedClass {
  static constexpr uint64_t kBoards = uint64_t{1'225} * 1'712'304;  // 2,097,572,400

  std::string_view name;
  uint64_t wins = 0;
  uint64_t ties = 0;

  // How many hands the class has: 6 for a pair, 4 suited, 12 offsuit.
  size_t hands() const;
  // The expected share of the pot: the wins plus half of every tie (heads-up, a tie halves it).
  double equity() const;
};

// All 169 classes, best first. No two have the same equity, so the order is unambiguous.
std::span<const RankedClass> handRanking();

// The best whole classes in handRanking(), as many as bring the number of hands closest to
// `percent` percent of all 1,326 (when two counts are equally close, the smaller one). For
// example, topRange(1) is "KK+": 12 hands, 0.9%, where QQ+ would be 18. 0 or less gives no
// hands, and 100 or more every hand.
Range topRange(double percent);

}  // namespace poker
