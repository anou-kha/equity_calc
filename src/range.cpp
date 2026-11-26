// Ranges of starting hands, and range notation like "QQ+, AKs, A5s-A2s".
//
// Every two-card hand ("combo") gets an index from its cards' indices: with hi > lo,
// index = hi * (hi - 1) / 2 + lo. That numbers the C(52,2) = 1,326 pairs of cards 0..1325, ordered
// by the higher card and then the lower one. A bitset over those indices makes "is this hand in the
// range?" a single lookup, and a vector kept sorted by index lists the hands in a canonical order.
#include "poker/range.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace poker {
namespace {

constexpr int kNumRanks = Card::kNumRanks;
constexpr int kNumSuits = Card::kNumSuits;

// The same hand with its higher-index card first.
HoleCards highCardFirst(HoleCards hand) {
  if (hand[0].index() < hand[1].index()) std::swap(hand[0], hand[1]);
  return hand;
}

size_t comboIndex(HoleCards hand) {
  const auto [high, low] = highCardFirst(hand);
  const auto hi = static_cast<size_t>(high.index());
  const auto lo = static_cast<size_t>(low.index());
  return hi * (hi - 1) / 2 + lo;
}

// ---- Hand classes like AKs ----------------------------------------------------------------------

enum class Suitedness { kAny, kSuited, kOffsuit };

// A group of hands described by ranks only: QQ, AKs, AKo or AK.
struct HandClass {
  int high = 0;  // the higher rank (for a pair, its rank)
  int low = 0;   // the lower rank (for a pair, the same rank)
  Suitedness suitedness = Suitedness::kAny;

  bool isPair() const { return high == low; }
};

// Calls visit(hand) for every hand in a class: 6 for a pair, 4 suited, 12 offsuit, 16 for both.
// visit returns false to stop early, and then so does this function.
template <typename Visit>
bool forEachHandInClass(int high, int low, Suitedness suitedness, const Visit& visit) {
  for (int suit1 = 0; suit1 < kNumSuits; ++suit1) {
    for (int suit2 = 0; suit2 < kNumSuits; ++suit2) {
      if (high == low && suit2 <= suit1) continue;  // a pair: each pair of suits once
      if (suitedness == Suitedness::kSuited && suit1 != suit2) continue;
      if (suitedness == Suitedness::kOffsuit && suit1 == suit2) continue;
      if (!visit(HoleCards{Card(high, suit1), Card(low, suit2)})) return false;
    }
  }
  return true;
}

void addClass(Range& range, int high, int low, Suitedness suitedness) {
  forEachHandInClass(high, low, suitedness, [&](HoleCards hand) {
    range.add(hand);
    return true;
  });
}

bool hasWholeClass(const Range& range, int high, int low, Suitedness suitedness) {
  return forEachHandInClass(high, low, suitedness,
                            [&](HoleCards hand) { return range.contains(hand); });
}

void addAllHands(Range& range) {
  for (int high = 1; high < Card::kNumCards; ++high) {
    for (int low = 0; low < high; ++low) range.add({Card::fromIndex(high), Card::fromIndex(low)});
  }
}

// Either the parsed hand class, or the reason it couldn't be parsed.
using HandClassOrError = std::variant<HandClass, std::string_view>;

// Parses "QQ", "AK", "AKs" or "AKo". The ranks may be written in either order.
HandClassOrError parseHandClass(std::string_view text) {
  if (text.size() < 2 || text.size() > 3) return "expected a hand like QQ, AK, AKs or AKo";
  const std::optional<int> first = parseRank(text[0]);
  const std::optional<int> second = parseRank(text[1]);
  if (!first || !second) return "not a rank (ranks are 2-9, T, J, Q, K, A)";

  HandClass hand{std::max(*first, *second), std::min(*first, *second), Suitedness::kAny};
  if (text.size() == 3) {
    if (hand.isPair()) return "a pair can't be suited or offsuit";
    const auto marker = static_cast<char>(std::tolower(static_cast<unsigned char>(text[2])));
    if (marker == 's') {
      hand.suitedness = Suitedness::kSuited;
    } else if (marker == 'o') {
      hand.suitedness = Suitedness::kOffsuit;
    } else {
      return "the last letter must be 's' (suited) or 'o' (offsuit)";
    }
  }
  return hand;
}

// ---- Entries -----------------------------------------------------------------------------------

std::string_view trim(std::string_view text) {
  const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  return text;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) ==
           std::tolower(static_cast<unsigned char>(y));
  });
}

// "JJ-99" or "A5s-A2s": every pair, or every kicker, from one end to the other (either order).
std::optional<std::string_view> addDashRange(Range& range, std::string_view from,
                                             std::string_view to) {
  const HandClassOrError first = parseHandClass(from);
  const HandClassOrError last = parseHandClass(to);
  if (const auto* reason = std::get_if<std::string_view>(&first)) return *reason;
  if (const auto* reason = std::get_if<std::string_view>(&last)) return *reason;
  const HandClass a = std::get<HandClass>(first);
  const HandClass b = std::get<HandClass>(last);

  if (a.isPair() && b.isPair()) {
    for (int rank = std::min(a.high, b.high); rank <= std::max(a.high, b.high); ++rank) {
      addClass(range, rank, rank, Suitedness::kAny);
    }
    return std::nullopt;
  }
  if (!a.isPair() && !b.isPair() && a.high == b.high && a.suitedness == b.suitedness) {
    for (int kicker = std::min(a.low, b.low); kicker <= std::max(a.low, b.low); ++kicker) {
      addClass(range, a.high, kicker, a.suitedness);
    }
    return std::nullopt;
  }
  return "both ends of a dash range must be pairs (JJ-99), or share the top card and "
         "suitedness (A5s-A2s)";
}

// Adds one comma-separated entry to the range, or returns why it can't.
std::optional<std::string_view> addEntry(Range& range, std::string_view entry) {
  if (entry.empty()) return "empty entry (two commas in a row?)";
  if (equalsIgnoringCase(entry, "random") || equalsIgnoringCase(entry, "any")) {
    addAllHands(range);
    return std::nullopt;
  }
  if (const size_t dash = entry.find('-'); dash != std::string_view::npos) {
    return addDashRange(range, trim(entry.substr(0, dash)), trim(entry.substr(dash + 1)));
  }

  const bool plus = entry.back() == '+';
  if (plus) entry.remove_suffix(1);

  // Four or more characters can only be a specific hand like "AhKh" (or "Ah Kh").
  if (entry.size() >= 4) {
    if (plus) return "'+' works on pairs and hand classes like ATs, not on a specific hand";
    const std::optional<HoleCards> hand = parseHoleCards(entry);
    if (!hand) return "not a valid hand (two different cards, like AhKh)";
    range.add(*hand);
    return std::nullopt;
  }

  const HandClassOrError parsed = parseHandClass(entry);
  if (const auto* reason = std::get_if<std::string_view>(&parsed)) return *reason;
  const HandClass hand = std::get<HandClass>(parsed);
  if (!plus) {
    addClass(range, hand.high, hand.low, hand.suitedness);
  } else if (hand.isPair()) {
    for (int rank = hand.high; rank < kNumRanks; ++rank) {
      addClass(range, rank, rank, Suitedness::kAny);  // QQ+: QQ, KK, AA
    }
  } else {
    for (int kicker = hand.low; kicker < hand.high; ++kicker) {
      addClass(range, hand.high, kicker, hand.suitedness);  // ATs+: ATs, AJs, AQs, AKs
    }
  }
  return std::nullopt;
}

// ---- Writing ranges -----------------------------------------------------------------------------

constexpr std::string_view kRankChars = "23456789TJQKA";

std::string className(int high, int low, Suitedness suitedness) {
  std::string name{kRankChars[static_cast<size_t>(high)], kRankChars[static_cast<size_t>(low)]};
  if (suitedness == Suitedness::kSuited) name += 's';
  if (suitedness == Suitedness::kOffsuit) name += 'o';
  return name;
}

// Which ranks (pairs) or kickers (with a given high card) form complete classes in a range.
using RankFlags = std::array<bool, kNumRanks>;

// Adds one entry per run of consecutive complete classes in `whole`, highest first. `top` is the
// highest rank a run can reach (A for pairs, one below the high card for kickers), where the run
// is written with "+"; name(rank) writes that rank's class.
template <typename Name>
void addRuns(std::vector<std::string>& entries, const RankFlags& whole, int top, const Name& name) {
  for (int rank = top; rank >= 0; --rank) {
    if (!whole[static_cast<size_t>(rank)]) continue;
    const int runHigh = rank;
    while (rank > 0 && whole[static_cast<size_t>(rank - 1)]) --rank;
    const int runLow = rank;
    if (runHigh == runLow) {
      entries.push_back(name(runHigh));  // "AA", "AKs"
    } else if (runHigh == top) {
      entries.push_back(name(runLow) + "+");  // "QQ+", "ATs+"
    } else {
      entries.push_back(name(runHigh) + "-" + name(runLow));  // "JJ-99", "A5s-A2s"
    }
  }
}

}  // namespace

void Range::add(HoleCards hand) {
  assert(hand[0].index() < Card::kNumCards && hand[1].index() < Card::kNumCards);
  assert(hand[0].index() != hand[1].index());
  const size_t index = comboIndex(hand);
  if (members_[index]) return;
  members_[index] = true;
  // Insert at the hand's place in canonical (combo index) order.
  const auto position =
      std::lower_bound(combos_.begin(), combos_.end(), index,
                       [](const HoleCards& h, size_t i) { return comboIndex(h) < i; });
  combos_.insert(position, highCardFirst(hand));
}

bool Range::contains(HoleCards hand) const { return members_[comboIndex(hand)]; }

std::variant<Range, RangeError> parseRange(std::string_view text) {
  if (trim(text).empty()) return RangeError{"", "the range is empty"};
  Range range;
  size_t start = 0;
  while (true) {
    const size_t comma = text.find(',', start);
    const std::string_view entry =
        trim(text.substr(start, comma == std::string_view::npos ? comma : comma - start));
    if (const auto reason = addEntry(range, entry)) return RangeError{std::string(entry), *reason};
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return range;
}

std::string formatRange(const Range& range) {
  if (range.size() == Range::kNumCombos) return "random";
  std::vector<std::string> entries;

  // Pairs.
  RankFlags wholePairs{};
  for (int rank = 0; rank < kNumRanks; ++rank) {
    wholePairs[static_cast<size_t>(rank)] = hasWholeClass(range, rank, rank, Suitedness::kAny);
  }
  addRuns(entries, wholePairs, kNumRanks - 1,
          [](int rank) { return className(rank, rank, Suitedness::kAny); });

  // Non-pairs, by high card. When the suited and offsuit classes are complete for exactly the same
  // kickers, one entry without s/o covers both ("AT+" rather than "ATs+, ATo+").
  std::array<RankFlags, kNumRanks> wholeSuited{};   // [high][kicker]
  std::array<RankFlags, kNumRanks> wholeOffsuit{};  // [high][kicker]
  for (int high = kNumRanks - 1; high > 0; --high) {
    RankFlags& suited = wholeSuited[static_cast<size_t>(high)];
    RankFlags& offsuit = wholeOffsuit[static_cast<size_t>(high)];
    for (int kicker = 0; kicker < high; ++kicker) {
      suited[static_cast<size_t>(kicker)] = hasWholeClass(range, high, kicker, Suitedness::kSuited);
      offsuit[static_cast<size_t>(kicker)] =
          hasWholeClass(range, high, kicker, Suitedness::kOffsuit);
    }
    const auto named = [high](Suitedness suitedness) {
      return [high, suitedness](int kicker) { return className(high, kicker, suitedness); };
    };
    if (suited == offsuit) {
      addRuns(entries, suited, high - 1, named(Suitedness::kAny));
    } else {
      addRuns(entries, suited, high - 1, named(Suitedness::kSuited));
      addRuns(entries, offsuit, high - 1, named(Suitedness::kOffsuit));
    }
  }

  // Hands whose class isn't complete, one by one, highest first. combos() lists each hand with
  // its higher card first, so hand[0] has the higher rank (or the same rank, for a pair).
  const auto combos = range.combos();
  for (auto it = combos.rbegin(); it != combos.rend(); ++it) {
    const HoleCards& hand = *it;
    const auto high = static_cast<size_t>(hand[0].rank());
    const auto low = static_cast<size_t>(hand[1].rank());
    const bool inWholeClass = high == low                        ? wholePairs[high]
                              : hand[0].suit() == hand[1].suit() ? wholeSuited[high][low]
                                                                 : wholeOffsuit[high][low];
    if (!inWholeClass) entries.push_back(hand[0].toString() + hand[1].toString());
  }

  std::string text;
  for (const std::string& entry : entries) {
    if (!text.empty()) text += ", ";
    text += entry;
  }
  return text;
}

}  // namespace poker
