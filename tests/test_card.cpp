#include <gtest/gtest.h>

#include <bit>

#include "poker/card.hpp"

using poker::Card;

TEST(Card, ParsesAndRoundTripsAllCards) {
  for (int i = 0; i < Card::kNumCards; ++i) {
    Card c = Card::fromIndex(i);
    auto parsed = Card::parse(c.toString());
    ASSERT_TRUE(parsed.has_value()) << c.toString();
    EXPECT_EQ(*parsed, c);
  }
}

TEST(Card, RankAndSuitEncoding) {
  auto ace_spades = Card::parse("As");
  ASSERT_TRUE(ace_spades);
  EXPECT_EQ(ace_spades->rank(), 12);
  EXPECT_EQ(ace_spades->suit(), 3);
  EXPECT_EQ(ace_spades->index(), 51);

  auto two_clubs = Card::parse("2c");
  ASSERT_TRUE(two_clubs);
  EXPECT_EQ(two_clubs->index(), 0);
}

TEST(Card, ParseIsCaseInsensitive) {
  EXPECT_EQ(Card::parse("tD"), Card::parse("Td"));
  EXPECT_EQ(Card::parse("aS"), Card::parse("As"));
}

TEST(Card, RejectsMalformedInput) {
  EXPECT_FALSE(Card::parse(""));
  EXPECT_FALSE(Card::parse("A"));
  EXPECT_FALSE(Card::parse("1s"));
  EXPECT_FALSE(Card::parse("Ax"));
  EXPECT_FALSE(Card::parse("Ass"));
}

TEST(ParseCards, ParsesRunsWithAndWithoutSpaces) {
  auto a = poker::parseCards("AsKs");
  auto b = poker::parseCards("  As Ks ");
  ASSERT_TRUE(a && b);
  EXPECT_EQ(a->size(), 2u);
  EXPECT_EQ(*a, *b);
}

TEST(ParseCards, RejectsDuplicatesAndOddLength) {
  EXPECT_FALSE(poker::parseCards("AsAs"));
  EXPECT_FALSE(poker::parseCards("AsK"));
}

TEST(ParseHoleCards, ParsesExactlyTwoDistinctCards) {
  const auto hole = poker::parseHoleCards("As Kd");
  ASSERT_TRUE(hole);
  EXPECT_EQ((*hole)[0], Card::parse("As"));
  EXPECT_EQ((*hole)[1], Card::parse("Kd"));
  EXPECT_TRUE(poker::parseHoleCards("AsKd"));
  EXPECT_FALSE(poker::parseHoleCards("As"));
  EXPECT_FALSE(poker::parseHoleCards("AsKdQh"));
  EXPECT_FALSE(poker::parseHoleCards("AsAs"));
  EXPECT_FALSE(poker::parseHoleCards(""));
}

TEST(ParseCards, MaskHasOneBitPerCard) {
  auto cards = poker::parseCards("JsTs2h");
  ASSERT_TRUE(cards);
  EXPECT_EQ(std::popcount(poker::toMask(*cards)), 3);
}
