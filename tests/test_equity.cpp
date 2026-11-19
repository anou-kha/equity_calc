#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "test_helpers.hpp"

namespace {

using poker::Card;
using poker::CardMask;
using poker::EquityError;
using poker::EquityResult;
using poker::HoleCards;
using poker::kPotUnits;
using poker::test::cards;

constexpr CardMask kFullDeck = (CardMask{1} << Card::kNumCards) - 1;

HoleCards hole(std::string_view text) {
  const auto parsed = poker::parseHoleCards(text);
  if (!parsed) throw std::invalid_argument("bad hole cards in test: " + std::string(text));
  return *parsed;
}

std::vector<HoleCards> holes(std::initializer_list<std::string_view> hands) {
  std::vector<HoleCards> players;
  for (std::string_view hand : hands) players.push_back(hole(hand));
  return players;
}

// Runs exactEquity on valid input; an error fails the test.
EquityResult equity(std::initializer_list<std::string_view> hands, std::string_view board = "",
                    std::string_view dead = "") {
  const auto outcome = poker::exactEquity(holes(hands), cards(board), cards(dead));
  if (const auto* error = std::get_if<EquityError>(&outcome)) {
    throw std::runtime_error("unexpected error: " + std::string(poker::toString(*error)));
  }
  return std::get<EquityResult>(outcome);
}

// Runs exactEquity on invalid input and returns the error (or fails the test if there was none).
EquityError errorFor(const std::vector<HoleCards>& players, CardMask board, CardMask dead = 0) {
  const auto outcome = poker::exactEquity(players, board, dead);
  if (!std::holds_alternative<EquityError>(outcome)) throw std::runtime_error("expected an error");
  return std::get<EquityError>(outcome);
}

// Every board's pot goes to somebody: the units handed out add up to exactly one pot per board.
void expectPotFullyDistributed(const EquityResult& result) {
  uint64_t units = 0;
  for (const poker::PlayerResult& p : result.players) units += p.wins * kPotUnits + p.tieShares;
  EXPECT_EQ(units, result.boards * kPotUnits);
}

// Published figures are percentages rounded to two decimals, so allow half of the last digit.
constexpr double kTwoDecimalPercent = 0.00005;

// ---- Reference values -------------------------------------------------------------------------
//
// Heads-up preflop values: "Preflop Poker Equity Matchups: Exact Data", DeucesCracked (2026),
// https://www.deucescracked.com/blog/preflop-poker-equity-matchups-exact-data, which reports win /
// win / tie percentages from full enumeration of all 1,712,304 boards. Its suits differ from ours,
// but only which suits the two hands share matters, and that matches in each case.
// Pokertube independently gives the AA vs KK figures rounded to 82.4% and 81.1%.

TEST(EquityReference, AcesVsKingsSharingBothSuits) {
  // Source lists A♥A♠ vs K♥K♠: 82.36% / 17.09% / 0.54% tie.
  const EquityResult r = equity({"AsAh", "KsKh"});
  EXPECT_EQ(r.boards, 1'712'304u);  // C(48, 5)
  EXPECT_NEAR(r.win(0), 0.8236, kTwoDecimalPercent);
  EXPECT_NEAR(r.win(1), 0.1709, kTwoDecimalPercent);
  EXPECT_NEAR(r.tie(0), 0.0054, kTwoDecimalPercent);
  expectPotFullyDistributed(r);
}

TEST(EquityReference, AcesVsKingsSharingNoSuits) {
  // Source lists A♥A♠ vs K♦K♣: 81.06% / 18.55% / 0.38% tie. With no shared suits the kings' flushes
  // are never dominated by a bigger flush, so they do about 1.3 points better than above.
  const EquityResult r = equity({"AhAs", "KdKc"});
  EXPECT_NEAR(r.win(0), 0.8106, kTwoDecimalPercent);
  EXPECT_NEAR(r.win(1), 0.1855, kTwoDecimalPercent);
  EXPECT_NEAR(r.tie(0), 0.0038, kTwoDecimalPercent);
  expectPotFullyDistributed(r);
}

TEST(EquityReference, AcesVsKingsSharingOneSuit) {
  // The commonly quoted "AA vs KK ≈ 81.9% / 18.1%" (CLAUDE.md) matches this middle case, which is
  // also close to the average over all suit combinations.
  const EquityResult r = equity({"AhAs", "KhKd"});
  EXPECT_NEAR(r.equity(0), 0.819, 0.001);
  EXPECT_NEAR(r.equity(1), 0.181, 0.001);
  expectPotFullyDistributed(r);
}

TEST(EquityReference, AceKingSuitedVsQueens) {
  // Source lists Q♥Q♠ vs A♦K♦: 53.59% / 46.02% / 0.39% tie, i.e. equity ≈ 53.79% / 46.21%.
  const EquityResult r = equity({"AsKs", "QhQd"});
  EXPECT_NEAR(r.win(0), 0.4602, kTwoDecimalPercent);
  EXPECT_NEAR(r.win(1), 0.5359, kTwoDecimalPercent);
  EXPECT_NEAR(r.tie(0), 0.0039, kTwoDecimalPercent);
  EXPECT_NEAR(r.equity(0), 0.4621, kTwoDecimalPercent);
  expectPotFullyDistributed(r);
}

TEST(EquityReference, ThreeWayPreflop) {
  // Ashley Adams, "Examining Three-Way All-In Situations in Hold'em", PokerNews (2020),
  // https://www.pokernews.com/strategy/examining-three-way-all-in-situations-in-hold-em-34123.htm:
  // K♠K♦ vs Q♣Q♥ vs 2♦2♠ wins "nearly 66 percent" / "about 19 percent" / "about 14.5 percent".
  // The article rounds loosely, so allow half a percentage point.
  const EquityResult r = equity({"KsKd", "QcQh", "2d2s"});
  EXPECT_EQ(r.boards, 1'370'754u);  // C(46, 5)
  EXPECT_NEAR(r.win(0), 0.66, 0.005);
  EXPECT_NEAR(r.win(1), 0.19, 0.005);
  EXPECT_NEAR(r.win(2), 0.145, 0.005);
  expectPotFullyDistributed(r);
}

// ---- Exact results ----------------------------------------------------------------------------

TEST(EquityExact, RiverWinnerTakesAll) {
  const EquityResult r = equity({"AhAd", "KcKd"}, "2c 7d 9h Ts 3s");
  EXPECT_EQ(r.boards, 1u);
  EXPECT_EQ(r.players[0].wins, 1u);
  EXPECT_EQ(r.equity(0), 1.0);
  EXPECT_EQ(r.equity(1), 0.0);
}

TEST(EquityExact, RiverChopWhenBoardPlays) {
  const EquityResult r = equity({"2c3d", "4h5h"}, "As Ks Qs Js Ts");  // royal flush on board
  EXPECT_EQ(r.tie(0), 1.0);
  EXPECT_EQ(r.equity(0), 0.5);
  EXPECT_EQ(r.equity(1), 0.5);
}

TEST(EquityExact, ThreeWayChopGivesEachPlayerAThird) {
  // A nine-high straight on the board; nobody holds a ten, so everyone plays the board.
  const EquityResult r = equity({"AhAd", "KcKd", "2h2s"}, "5c 6d 7h 8s 9c");
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(r.players[i].tieShares, kPotUnits / 3);
    EXPECT_EQ(r.equity(i), 1.0 / 3.0);  // exact: both sides are the double closest to 1/3
  }
}

TEST(EquityExact, TwoPlayersChopAndOneLoses) {
  // Both aces make Aces and Sevens with a King kicker; Kings and Sevens loses to them.
  const EquityResult r = equity({"Ah3d", "As4d", "KhQd"}, "Ac Kd 7h 7s 2c");
  EXPECT_EQ(r.equity(0), 0.5);
  EXPECT_EQ(r.equity(1), 0.5);
  EXPECT_EQ(r.equity(2), 0.0);
  EXPECT_EQ(r.tie(2), 0.0);
}

// A flop spot worked out by hand. Kings have top set. The queens' only way to win is runner-runner
// quads: the turn and river must be exactly Qh and Qd (Qc, Qs are in their hand).
//  - Straight: Q with K, 7 or 2 needs three more cards, and only two are coming.
//  - Flush: the queens hold one club and one spade, and the flop has one of each.
//  - Queens full (e.g. 7-7 runout) still loses to kings full. No board can make them tie.
// That is 1 winning board out of C(45, 2) = 990.
TEST(EquityExact, FlopSetVersusUnderpairWorkedOutByHand) {
  const EquityResult r = equity({"KhKd", "QcQs"}, "Kc 7s 2d");
  EXPECT_EQ(r.boards, 990u);
  EXPECT_EQ(r.players[0].wins, 989u);
  EXPECT_EQ(r.players[1].wins, 1u);
  EXPECT_EQ(r.players[0].ties, 0u);
}

TEST(EquityExact, DeadCardRemovesTheOnlyOut) {
  // Same spot, but the Qh is dead, so the queens can no longer make quads.
  const EquityResult r = equity({"KhKd", "QcQs"}, "Kc 7s 2d", "Qh");
  EXPECT_EQ(r.boards, 946u);  // C(44, 2)
  EXPECT_EQ(r.equity(0), 1.0);
}

// ---- Board counts -----------------------------------------------------------------------------

TEST(EquityBoards, CountsMatchCombinations) {
  EXPECT_EQ(equity({"AsKs", "QhQd"}, "Js Ts 2h").boards, 990u);  // C(45, 2)
  EXPECT_EQ(equity({"AsKs", "QhQd"}, "Js Ts 2h 3c").boards, 44u);
  EXPECT_EQ(equity({"AsKs", "QhQd"}, "Js Ts 2h 3c 4d").boards, 1u);
  EXPECT_EQ(equity({"AsKs", "QhQd", "7c7d"}, "Js Ts 2h").boards, 903u);  // C(43, 2)
  EXPECT_EQ(equity({"AsKs", "QhQd"}, "Js Ts 2h", "9c").boards, 946u);    // C(44, 2)
}

// ---- Invariants -------------------------------------------------------------------------------

TEST(EquityInvariants, SwappingPlayersSwapsResults) {
  const EquityResult a = equity({"AsKs", "QhQd"}, "Js Ts 2h");
  const EquityResult b = equity({"QhQd", "AsKs"}, "Js Ts 2h");
  EXPECT_EQ(a.players[0].wins, b.players[1].wins);
  EXPECT_EQ(a.players[1].wins, b.players[0].wins);
  EXPECT_EQ(a.players[0].ties, b.players[1].ties);
}

// Suits are symmetric: renaming them consistently everywhere can't change who wins.
TEST(EquityInvariants, RelabellingSuitsChangesNothing) {
  const EquityResult original = equity({"AsKs", "QhQd", "7c6c"}, "Js Ts 2h");
  // spades -> hearts, hearts -> diamonds, diamonds -> clubs, clubs -> spades
  const EquityResult relabelled = equity({"AhKh", "QdQc", "7s6s"}, "Jh Th 2d");
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(original.players[i].wins, relabelled.players[i].wins);
    EXPECT_EQ(original.players[i].ties, relabelled.players[i].ties);
    EXPECT_EQ(original.players[i].tieShares, relabelled.players[i].tieShares);
  }
}

TEST(EquityInvariants, PotIsFullyDistributedMultiway) {
  expectPotFullyDistributed(equity({"AsKs", "QhQd", "7c7d", "5h4h"}, "Js Ts 2h"));
  expectPotFullyDistributed(equity({"2c3d", "2d3c", "2h3s", "2s3h"}, "Ac Kd 7h"));  // many ties
}

// ---- Validation -------------------------------------------------------------------------------

TEST(EquityValidation, PlayerCount) {
  EXPECT_EQ(errorFor(holes({"AsKs"}), 0), EquityError::kTooFewPlayers);
  EXPECT_EQ(errorFor(holes({"2c2d", "3c3d", "4c4d", "5c5d", "6c6d", "7c7d", "8c8d", "9c9d", "TcTd",
                            "JcJd"}),
                     0),
            EquityError::kTooManyPlayers);
  // Nine players is allowed: 18 hole cards still leave plenty to deal.
  EXPECT_EQ(
      equity({"2c2d", "3c3d", "4c4d", "5c5d", "6c6d", "7c7d", "8c8d", "9c9d", "TcTd"}, "Ah Kh Qh")
          .boards,
      465u);  // C(31, 2)
}

TEST(EquityValidation, BoardSize) {
  const auto players = holes({"AsKs", "QhQd"});
  EXPECT_EQ(errorFor(players, cards("2c")), EquityError::kBadBoardSize);
  EXPECT_EQ(errorFor(players, cards("2c 3c")), EquityError::kBadBoardSize);
  EXPECT_EQ(errorFor(players, cards("2c 3c 4c 5c 6c 7c")), EquityError::kBadBoardSize);
}

TEST(EquityValidation, DuplicateCards) {
  EXPECT_EQ(errorFor(holes({"AsKs", "AsQd"}), 0), EquityError::kDuplicateCard);  // two hands
  EXPECT_EQ(errorFor(holes({"AsKs", "QhQd"}), cards("As 2c 3c")),
            EquityError::kDuplicateCard);  // hand and board
  EXPECT_EQ(errorFor(holes({"AsKs", "QhQd"}), 0, cards("Ks")),
            EquityError::kDuplicateCard);  // hand and dead
  EXPECT_EQ(errorFor(holes({"AsKs", "QhQd"}), cards("2c 3c 4c"), cards("2c")),
            EquityError::kDuplicateCard);  // board and dead
  const Card aceOfSpades = *Card::parse("As");
  EXPECT_EQ(errorFor({HoleCards{aceOfSpades, aceOfSpades}, hole("QhQd")}, 0),
            EquityError::kDuplicateCard);  // the same card twice in one hand
}

TEST(EquityValidation, InvalidCards) {
  const HoleCards offTheDeck{Card::fromIndex(Card::kNumCards), Card::fromIndex(0)};
  EXPECT_EQ(errorFor({offTheDeck, hole("QhQd")}, 0), EquityError::kInvalidCard);
  EXPECT_EQ(errorFor(holes({"AsKs", "QhQd"}), CardMask{1} << 60), EquityError::kInvalidCard);
}

TEST(EquityValidation, NotEnoughCardsLeft) {
  const CardMask used = cards("AsAh KsKh");
  // Kill everything except 2c 3c 4c 5c: four cards can't make a five-card board.
  const CardMask allButFour = kFullDeck & ~used & ~cards("2c 3c 4c 5c");
  EXPECT_EQ(errorFor(holes({"AsAh", "KsKh"}), 0, allButFour), EquityError::kNotEnoughCards);
  // Exactly five cards left is fine: there is exactly one possible board.
  const CardMask allButFive = kFullDeck & ~used & ~cards("2c 3c 4c 5c 7d");
  const auto outcome = poker::exactEquity(holes({"AsAh", "KsKh"}), 0, allButFive);
  ASSERT_TRUE(std::holds_alternative<EquityResult>(outcome));
  EXPECT_EQ(std::get<EquityResult>(outcome).boards, 1u);
}

TEST(EquityValidation, EveryErrorHasAMessage) {
  for (const EquityError e :
       {EquityError::kTooFewPlayers, EquityError::kTooManyPlayers, EquityError::kInvalidCard,
        EquityError::kBadBoardSize, EquityError::kDuplicateCard, EquityError::kNotEnoughCards}) {
    EXPECT_FALSE(poker::toString(e).empty());
  }
}

}  // namespace
