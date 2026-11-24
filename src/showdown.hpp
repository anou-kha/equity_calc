#pragma once

// Internal building blocks shared by exact enumeration (equity.cpp) and Monte Carlo
// (monte_carlo.cpp): input validation, the remaining deck, and scoring a showdown. Keeping them in
// one place means both methods score boards with exactly the same code, so tests comparing the two
// only test how the boards are chosen. Not part of the public API.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/range.hpp"

namespace poker::detail {

inline constexpr int kBoardSize = 5;

// Checks the inputs to the known-hand (HoleCards) versions of exactEquity() and
// monteCarloEquity().
std::optional<EquityError> validateDeal(std::span<const HoleCards> players, CardMask board,
                                        CardMask dead);

// The same hands as one-hand ranges, so that known hands can go through the range code.
std::vector<Range> singleHandRanges(std::span<const HoleCards> players);

// The number of board cards still to be dealt.
size_t missingBoardCards(CardMask board);

// The players' hole cards as masks, in a fixed-size array (no heap allocation).
struct Seats {
  std::array<CardMask, kMaxPlayers> holes{};
  size_t count = 0;
  CardMask allHoleCards = 0;

  std::span<const CardMask> holeMasks() const { return {holes.data(), count}; }
};

// Each player's live hands: the hands in their range that don't use a board or dead card, as card
// masks in the range's canonical order.
using LiveHands = std::vector<std::vector<CardMask>>;

// Checks the inputs to the range versions of exactEquity() and monteCarloEquity() (player count,
// cards, board size, enough cards left) and works out each player's live hands. A range with no
// live hands is kEmptyRange (kDuplicateCard for a one-hand range). Whether all the ranges can be
// dealt at once is checked separately, with forEachDeal().
std::variant<LiveHands, EquityError> liveHands(std::span<const Range> players, CardMask board,
                                               CardMask dead);

// The error when no valid deal exists: kNoValidDeal, or kDuplicateCard if every range is a single
// known hand, so known hands get the same error as in the HoleCards versions.
EquityError noValidDealError(std::span<const Range> players);

// The cards left in the deck once the board, the dead cards and every player's hand are out.
// Every valid deal leaves the same number, which is what makes all deals equally weighted.
int cardsLeftAfterDeal(CardMask board, CardMask dead, size_t numPlayers);

template <typename Visit>
bool dealFrom(const LiveHands& hands, size_t player, CardMask used, Seats& seats,
              const Visit& visit) {
  if (player == hands.size()) {
    seats.allHoleCards = used;
    return visit(static_cast<const Seats&>(seats));
  }
  for (const CardMask hand : hands[player]) {
    if (hand & used) continue;  // shares a card with an earlier player's hand
    seats.holes[player] = hand;
    if (!dealFrom(hands, player + 1, used | hand, seats, visit)) return false;
  }
  return true;
}

// Calls visit(seats) for every deal: one live hand for each player, with no card used twice.
// `visit` returns false to stop early, and forEachDeal() then returns false as well.
template <typename Visit>
bool forEachDeal(const LiveHands& hands, const Visit& visit) {
  Seats seats;
  seats.count = hands.size();
  return dealFrom(hands, 0, 0, seats, visit);
}

// The cards that can still be dealt, each as a single-bit mask, in a fixed-size array.
struct Deck {
  std::array<CardMask, Card::kNumCards> cards{};
  size_t size = 0;
};

Deck remainingDeck(CardMask used);

// How many pot units (see kPotUnits) each player wins on one complete board: all of them for an
// outright win, kPotUnits / k for each of k players sharing the best hand, and 0 otherwise.
using PotShares = std::array<uint64_t, kMaxPlayers>;

PotShares showdown(CardMask board, std::span<const CardMask> holes);

// Adds one showdown to the per-player counts: a whole pot is a win, part of a pot is a tie.
void addToCounts(const PotShares& shares, std::span<PlayerResult> counts);

// Adds one set of per-player counts into another, e.g. to combine the results of several threads.
void addCounts(std::span<const PlayerResult> from, std::span<PlayerResult> into);

}  // namespace poker::detail
