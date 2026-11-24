#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/range.hpp"

namespace poker {

inline constexpr size_t kMinPlayers = 2;
inline constexpr size_t kMaxPlayers = 9;

// Split pots are counted in units of 1/2520 of a pot. 2520 = lcm(1, 2, ..., 9) is divisible by
// every possible number of tied players, so each share of a k-way split (2520 / k units) is a
// whole number and all the totals stay exact integers. A 3-way chop gives each player 840 units.
inline constexpr uint64_t kPotUnits = 2520;

struct PlayerResult {
  uint64_t wins = 0;       // boards this player wins outright
  uint64_t ties = 0;       // boards where this player ties for the best hand
  uint64_t tieShares = 0;  // this player's share of the tied pots, in units of 1/kPotUnits
};

struct EquityResult {
  std::vector<PlayerResult> players;  // in the order the players were given
  uint64_t boards = 0;                // showdowns scored: deals x board completions (or trials)
  uint64_t deals = 0;                 // exact only: ways of dealing the players' hands (1 if known)
  unsigned threads = 1;               // how many threads did the work

  // Fractions of all showdowns, in [0, 1].
  double win(size_t player) const;
  double tie(size_t player) const;
  // The player's expected share of the pot: wins, plus their split of each tied pot.
  double equity(size_t player) const;
};

enum class EquityError {
  kTooFewPlayers,   // fewer than kMinPlayers
  kTooManyPlayers,  // more than kMaxPlayers
  kInvalidCard,     // a card index outside [0, 52)
  kBadBoardSize,    // the board must have 0, 3, 4 or 5 cards
  kDuplicateCard,   // the same card appears twice among hole cards, board and dead cards
  kNotEnoughCards,  // the dead cards leave too few cards to complete the board
  kNoTrials,        // Monte Carlo was asked for 0 trials
  kEmptyRange,      // a range has no hands left once the board and dead cards are removed
  kNoValidDeal,     // the ranges can't all be dealt at once (e.g. AA against AA against AA)
  // For ranges of a single (known) hand, those two cases are reported as kDuplicateCard, just as
  // by the HoleCards versions.
};

std::string_view toString(EquityError error);

struct ExactOptions {
  // How many threads may share the work: 1 (the default) uses only the calling thread, and 0 means
  // one per hardware thread. Small jobs use fewer threads, because starting a thread costs more
  // than enumerating a few thousand boards. The result never depends on the number of threads.
  unsigned threads = 1;
};

// Exact all-in equity for known hands: deals every possible completion of the board from the cards
// nobody holds (excluding `dead` cards) and records who wins or ties on each one.
//
// The board may have 0 (preflop), 3 (flop), 4 (turn) or 5 (river) cards. Heads-up preflop that is
// C(48, 5) = 1,712,304 boards. Returns an error instead of a result if the input is invalid.
std::variant<EquityResult, EquityError> exactEquity(std::span<const HoleCards> players,
                                                    CardMask board, CardMask dead = 0,
                                                    const ExactOptions& options = {});

// Exact all-in equity when players hold ranges: every deal (one hand from each player's range, no
// card used twice) combined with every completion of the board. Hands that use a board or dead
// card are left out of their range. Every deal leaves the same number of cards to complete the
// board from, so every (deal, board) pair is equally likely and counts once. A known hand is a
// range with one hand, and gives exactly the same result as the HoleCards version.
std::variant<EquityResult, EquityError> exactEquity(std::span<const Range> players, CardMask board,
                                                    CardMask dead = 0,
                                                    const ExactOptions& options = {});

// How many showdowns exactEquity() would score for these ranges, so callers can decide between it
// and monteCarloEquity(). Counting the deals of wide ranges can itself take a while, so counting
// stops once it passes `limit`: the result is exact up to `limit`, and otherwise just some number
// above it. Returns 0 if the input is invalid (exactEquity() then reports the error).
uint64_t exactShowdowns(std::span<const Range> players, CardMask board, CardMask dead,
                        uint64_t limit);

}  // namespace poker
