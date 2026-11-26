// The hand ranking. Computing it takes about 355 billion showdowns (169 classes x 2.1 billion), so
// tools/rank_hands.cpp computes it once with exactEquity(), and the result is stored here.
#include "poker/hand_ranking.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>

namespace poker {
namespace {

constexpr std::array<RankedClass, 169> kRanking = {{
#include "hand_ranking_table.inc"
}};

}  // namespace

size_t RankedClass::hands() const {
  if (name.size() == 2) return 6;  // a pair
  return name.back() == 's' ? 4 : 12;
}

double RankedClass::equity() const {
  return static_cast<double>(2 * wins + ties) / static_cast<double>(2 * kBoards);
}

std::span<const RankedClass> handRanking() { return kRanking; }

Range topRange(double percent) {
  Range range;
  if (!(percent > 0)) return range;  // also catches NaN
  const double target = std::min(percent, 100.0) / 100.0 * static_cast<double>(Range::kNumCombos);
  // The distance to the target shrinks as classes are added, then grows: stop at the bottom.
  size_t count = 0;
  for (const RankedClass& handClass : kRanking) {
    const size_t withClass = count + handClass.hands();
    if (std::abs(static_cast<double>(withClass) - target) >=
        std::abs(static_cast<double>(count) - target)) {
      break;
    }
    // Kept in a variable: a range-for over a temporary's combos() would read freed memory.
    const Range hands = std::get<Range>(parseRange(handClass.name));
    for (const HoleCards& hand : hands.combos()) range.add(hand);
    count = withClass;
  }
  return range;
}

}  // namespace poker
