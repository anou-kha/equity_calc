#pragma once

#include <bitset>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/card.hpp"

namespace poker {

// A set of starting hands: any of the C(52,2) = 1,326 possible two-card hands ("combos").
// A known hand is simply a range with one combo.
class Range {
 public:
  static constexpr size_t kNumCombos = 1326;

  // Adds a hand. The order of its two cards doesn't matter, and adding a hand that is already in
  // the range changes nothing. The two cards must be valid and different.
  void add(HoleCards hand);

  bool contains(HoleCards hand) const;
  size_t size() const { return combos_.size(); }
  bool empty() const { return combos_.empty(); }

  // The hands in a canonical order (by their higher card, then their lower card), with each
  // hand's higher card first. The order doesn't depend on how the range was built, so "QQ+, AKs"
  // and "AKs, QQ+" give exactly the same results, even for seeded Monte Carlo.
  std::span<const HoleCards> combos() const { return combos_; }

  friend bool operator==(const Range& a, const Range& b) { return a.members_ == b.members_; }

 private:
  std::bitset<kNumCombos> members_;  // members_[i]: is the combo with index i in the range?
  std::vector<HoleCards> combos_;    // the same hands, kept in canonical order
};

struct RangeError {
  std::string token;        // the entry that couldn't be read, e.g. "AKx"
  std::string_view reason;  // why, e.g. "a suited/offsuit marker must be 's' or 'o'"
};

// Parses range notation: comma-separated entries, each one of
//   QQ           a pair (6 combos)
//   AKs AKo AK   suited (4), offsuit (12), or both (16)
//   QQ+          that pair and every higher one: QQ, KK, AA
//   ATs+         the kicker rises up to just below the top card: ATs, AJs, AQs, AKs
//   JJ-99        every pair from one to the other
//   A5s-A2s      same top card and suitedness, every kicker in between
//   AhKh         one specific hand
//   random       all 1,326 hands (also "any")
// Spaces are ignored, ranks may be upper or lower case, and duplicates count once.
std::variant<Range, RangeError> parseRange(std::string_view text);

// Writes a range in standard notation, the reverse of parseRange(): parseRange(formatRange(r))
// always gives back exactly r. Hand classes that are complete (all 6, 4 or 12 hands) are grouped
// into runs, like "QQ+, JJ-99, ATs+, A5s-A2s, KQ" (no s/o when suited and offsuit match); any other
// hands are listed one by one ("AsKs, AhKh"); all 1,326 hands are "random", and no hands are "".
// The order is fixed (pairs, then by high card, suited before offsuit, then single hands), so equal
// ranges always give the same text.
std::string formatRange(const Range& range);

}  // namespace poker
