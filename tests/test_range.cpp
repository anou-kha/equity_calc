#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/range.hpp"
#include "test_helpers.hpp"

namespace {

using poker::Range;
using poker::RangeError;
using poker::test::hole;

Range range(std::string_view text) {
  const auto parsed = poker::parseRange(text);
  if (const auto* error = std::get_if<RangeError>(&parsed)) {
    throw std::invalid_argument("bad range in test: \"" + std::string(text) + "\" (" +
                                error->token + ": " + std::string(error->reason) + ")");
  }
  return std::get<Range>(parsed);
}

RangeError errorFor(std::string_view text) {
  const auto parsed = poker::parseRange(text);
  if (!std::holds_alternative<RangeError>(parsed)) {
    throw std::runtime_error("expected \"" + std::string(text) + "\" to be rejected");
  }
  return std::get<RangeError>(parsed);
}

// ---- Notation ---------------------------------------------------------------------------------

TEST(RangeParsing, ComboCounts) {
  struct Case {
    const char* text;
    size_t combos;
  };
  const Case cases[] = {
      {"QQ", 6},        {"AKs", 4},       {"AKo", 12},
      {"AK", 16},       {"QQ+", 18},      {"22+", 78},
      {"ATs+", 16},     {"KTo+", 36},     {"JJ-99", 18},
      {"A5s-A2s", 16},  {"KQo-K9o", 48},  {"AhKh", 1},
      {"random", 1326}, {"any", 1326},    {"QQ+, AKs", 22},
      {"AA, AA", 6},    {"AKs, AhKh", 4}, {"QQ+, AK, 99-77", 52},
  };
  for (const Case& c : cases) EXPECT_EQ(range(c.text).size(), c.combos) << c.text;
}

TEST(RangeParsing, ClassesContainTheRightHands) {
  const Range suited = range("AKs");
  EXPECT_TRUE(suited.contains(hole("AsKs")));
  EXPECT_TRUE(suited.contains(hole("KhAh")));  // the order of the two cards doesn't matter
  EXPECT_FALSE(suited.contains(hole("AsKh")));

  const Range offsuit = range("AKo");
  EXPECT_TRUE(offsuit.contains(hole("AsKh")));
  EXPECT_FALSE(offsuit.contains(hole("AsKs")));

  const Range pairs = range("QQ+");
  EXPECT_TRUE(pairs.contains(hole("QhQd")));
  EXPECT_TRUE(pairs.contains(hole("AsAc")));
  EXPECT_FALSE(pairs.contains(hole("JhJd")));

  const Range kickers = range("ATs+");
  EXPECT_TRUE(kickers.contains(hole("AhTh")));
  EXPECT_TRUE(kickers.contains(hole("AhKh")));
  EXPECT_FALSE(kickers.contains(hole("Ah9h")));

  const Range dash = range("A5s-A2s");
  EXPECT_TRUE(dash.contains(hole("Ah5h")));
  EXPECT_TRUE(dash.contains(hole("Ah2h")));
  EXPECT_FALSE(dash.contains(hole("Ah6h")));
  EXPECT_FALSE(dash.contains(hole("Ah5d")));
}

// Every pair (78 hands) plus every two-rank class in both suitednesses (78 classes x 16 hands)
// is every hand there is: 78 + 1,248 = 1,326.
TEST(RangeParsing, EveryPairAndClassAddsUpToRandom) {
  EXPECT_EQ(range("22+, A2+, K2+, Q2+, J2+, T2+, 92+, 82+, 72+, 62+, 52+, 42+, 32"),
            range("random"));
}

TEST(RangeParsing, OrderCaseSpacesAndDuplicatesDontMatter) {
  EXPECT_EQ(range("QQ+, AKs"), range(" aks ,qq+,AA "));
  EXPECT_EQ(range("KA"), range("AK"));
  EXPECT_EQ(range("KsAs"), range("AsKs"));
  EXPECT_EQ(range("99-JJ"), range("JJ-99"));
  EXPECT_EQ(range("A2s-A5s"), range("A5s - A2s"));
}

// ---- Canonical order --------------------------------------------------------------------------

TEST(Range, CombosAreInTheSameOrderHoweverTheRangeIsWritten) {
  const Range a = range("QQ+, AKs, 72o");
  const Range b = range("72o, AKs, AA, KK, QQ");
  ASSERT_EQ(a.size(), b.size());
  EXPECT_TRUE(std::equal(a.combos().begin(), a.combos().end(), b.combos().begin()));
}

TEST(Range, CombosAreSortedWithTheHigherCardFirst) {
  const Range all = range("random");
  const auto combos = all.combos();
  for (size_t i = 0; i < combos.size(); ++i) {
    EXPECT_GT(combos[i][0].index(), combos[i][1].index());
    if (i > 0) {
      const auto previous = std::pair(combos[i - 1][0].index(), combos[i - 1][1].index());
      EXPECT_LT(previous, std::pair(combos[i][0].index(), combos[i][1].index()));
    }
  }
}

// ---- Errors -----------------------------------------------------------------------------------

TEST(RangeParsing, ErrorsNameTheBadEntry) {
  struct Case {
    const char* text;
    const char* badEntry;
  };
  const Case cases[] = {
      {"AA, AKx", "AKx"},      // not 's' or 'o'
      {"AA,,KK", ""},          // empty entry
      {"QQ+,", ""},            // trailing comma
      {"AAs", "AAs"},          // a pair can't be suited
      {"Z2", "Z2"},            // not a rank
      {"A5s-K2s", "A5s-K2s"},  // dash ends with different top cards
      {"QQ-AKs", "QQ-AKs"},    // dash from a pair to a non-pair
      {"AKs-", "AKs-"},        // dash with nothing after it
      {"AsKs+", "AsKs+"},      // '+' on a specific hand
      {"AsAs", "AsAs"},        // the same card twice
      {"AsKsQs", "AsKsQs"},    // three cards
      {"   ", ""},             // nothing at all
  };
  for (const Case& c : cases) {
    const RangeError error = errorFor(c.text);
    EXPECT_EQ(error.token, c.badEntry) << c.text;
    EXPECT_FALSE(error.reason.empty()) << c.text;
  }
}

// ---- Writing ranges: formatRange ----------------------------------------------------------------

TEST(RangeFormatting, StandardRangesComeOutInStandardForm) {
  struct Case {
    std::string_view text;
    std::string_view formatted;
  };
  const Case cases[] = {
      {"QQ+, AKs", "QQ+, AKs"},
      {"AKs, QQ+", "QQ+, AKs"},  // the order is fixed
      {"AA", "AA"},
      {"KK, AA", "KK+"},
      {"22+", "22+"},
      {"JJ-99", "JJ-99"},
      {"55, 33, 22", "55, 33-22"},
      {"ATs+", "ATs+"},
      {"KQs, KJs, KTs", "KTs+"},  // a run up to the top kicker is written with "+"
      {"A5s-A2s", "A5s-A2s"},
      {"K9o-K6o", "K9o-K6o"},
      {"AKs, AKo", "AK"},                        // suited and offsuit together
      {"ATs+, ATo+", "AT+"},                     // ... also for runs
      {"ATs+, AQo+", "ATs+, AQo+"},              // but not when they differ
      {"KhAh", "AhKh"},                          // a single hand: higher card first
      {"AhKh, AsKs, AdKd", "AsKs, AhKh, AdKd"},  // an incomplete class: hand by hand
      {"AKs, AsKd", "AKs, AsKd"},
      {"QQ+, AhAs", "QQ+"},  // a hand already in the range
      {"any", "random"},
  };
  for (const Case& c : cases) EXPECT_EQ(poker::formatRange(range(c.text)), c.formatted) << c.text;
  EXPECT_EQ(poker::formatRange(Range{}), "");
}

// parseRange(formatRange(r)) must give back exactly r, and writing that again the same text.
void expectRoundTrip(const Range& original) {
  const std::string text = poker::formatRange(original);
  const Range back = range(text);
  EXPECT_EQ(back, original) << text;
  EXPECT_EQ(poker::formatRange(back), text);
}

// Every hand class's own name, like "AA", "AKs", "AKo" and "AK" (suited and offsuit).
std::vector<std::string> everyClassName() {
  constexpr std::string_view kRanks = "23456789TJQKA";
  std::vector<std::string> names;
  for (size_t high = 0; high < kRanks.size(); ++high) {
    for (size_t low = 0; low <= high; ++low) {
      const std::string name{kRanks[high], kRanks[low]};
      if (high == low) {
        names.push_back(name);
      } else {
        names.insert(names.end(), {name + "s", name + "o", name});
      }
    }
  }
  return names;
}

TEST(RangeFormatting, EveryHandClassIsWrittenAsItself) {
  const std::vector<std::string> names = everyClassName();
  EXPECT_EQ(names.size(), 13u + 78u * 3);
  for (const std::string& name : names) {
    EXPECT_EQ(poker::formatRange(range(name)), name);
    expectRoundTrip(range(name));
  }
}

TEST(RangeFormatting, EverySingleHandRoundTrips) {
  for (int high = 1; high < poker::Card::kNumCards; ++high) {
    for (int low = 0; low < high; ++low) {
      Range single;
      single.add({poker::Card::fromIndex(low), poker::Card::fromIndex(high)});
      const std::string text = poker::formatRange(single);
      EXPECT_EQ(text,
                poker::Card::fromIndex(high).toString() + poker::Card::fromIndex(low).toString());
      expectRoundTrip(single);
    }
  }
}

// Seeded random mixtures of whole classes and single hands, from sparse to almost everything.
TEST(RangeFormatting, RandomRangesRoundTrip) {
  const std::vector<std::string> names = everyClassName();
  std::mt19937_64 rng(2026);  // raw output: the same ranges on every platform
  for (int trial = 0; trial < 2000; ++trial) {
    const uint64_t classOdds = 2 + static_cast<uint64_t>(trial % 5);  // 1 in 2 ... 1 in 6
    Range mixed;
    for (const std::string& name : names) {
      if (rng() % classOdds == 0) {
        // Not `for (... : range(name).combos())`: combos() is a view into the range, and a
        // temporary range would be destroyed before the loop runs.
        const Range wholeClass = range(name);
        for (const poker::HoleCards& hand : wholeClass.combos()) mixed.add(hand);
      }
    }
    for (int high = 1; high < poker::Card::kNumCards; ++high) {
      for (int low = 0; low < high; ++low) {
        if (rng() % 25 == 0) mixed.add({poker::Card::fromIndex(high), poker::Card::fromIndex(low)});
      }
    }
    expectRoundTrip(mixed);
    if (HasFailure()) return;  // one failing range is enough to see the problem
  }
}

TEST(RangeFormatting, EverythingButOneHandIsNotRandom) {
  const Range all = range("random");
  Range almostAll;
  for (const poker::HoleCards& hand : all.combos()) {
    if (hand != hole("AsKs")) almostAll.add(hand);
  }
  EXPECT_EQ(almostAll.size(), 1325u);
  EXPECT_NE(poker::formatRange(almostAll), "random");
  expectRoundTrip(almostAll);
}

}  // namespace
