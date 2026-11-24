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

#include "poker/card.hpp"
#include "poker/equity.hpp"

namespace poker::detail {

inline constexpr int kBoardSize = 5;

// Checks the inputs common to exactEquity() and monteCarloEquity().
std::optional<EquityError> validateDeal(std::span<const HoleCards> players, CardMask board,
                                        CardMask dead);

// The number of board cards still to be dealt.
size_t missingBoardCards(CardMask board);

// The players' hole cards as masks, in a fixed-size array (no heap allocation).
struct Seats {
  std::array<CardMask, kMaxPlayers> holes{};
  size_t count = 0;
  CardMask allHoleCards = 0;

  std::span<const CardMask> holeMasks() const { return {holes.data(), count}; }
};

// Requires a valid deal (see validateDeal), in particular at most kMaxPlayers players.
Seats seatPlayers(std::span<const HoleCards> players);

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
