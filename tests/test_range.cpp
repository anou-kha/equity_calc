#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

}  // namespace
