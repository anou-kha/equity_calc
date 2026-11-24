// Monte Carlo equity.
//
// Idea: instead of visiting every possible board, deal many random boards and average the results.
// Each trial's pot share is a random sample whose average is the true equity, so the estimate's
// error shrinks like 1/sqrt(trials): four times as many trials halve the error. The sample variance
// tells us how big that error is, which is what the confidence interval reports.
//
// Trade-off: for known hole cards, exact enumeration is cheap enough that Monte Carlo rarely pays
// off; it becomes essential once players hold ranges of hands and the exact space explodes.
//
// Multithreading: trials run in fixed-size chunks, and threads take chunks one at a time. Each
// chunk's random stream and starting deck depend only on its number, every thread counts into its
// own totals, and the totals are integers, so the combined result is identical whatever the number
// of threads or the order they finish in.
#include "poker/monte_carlo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include "parallel.hpp"
#include "poker/random.hpp"
#include "showdown.hpp"

namespace poker {
namespace {

// Trials run in chunks of this size. Chunk c always uses random stream c and starts from the same
// deck order, so its results don't depend on any other chunk. (Changing the size changes which
// boards a given seed deals.)
constexpr uint64_t kTrialsPerChunk = uint64_t{1} << 16;

// One thread's totals, in fixed-size arrays so the hot loop never allocates.
struct Totals {
  std::array<PlayerResult, kMaxPlayers> players{};
  std::array<uint64_t, kMaxPlayers> squaredPotUnits{};
  uint64_t trials = 0;
};

// Deals `count` random cards to complete `board` by partially shuffling the deck (Fisher-Yates):
// position i swaps with a uniformly chosen position from i onwards, so the first `count` cards are
// a uniformly random selection. The deck doesn't need resetting between trials: shuffling an
// already-shuffled deck is still uniform.
CardMask dealRandomBoard(detail::Deck& deck, size_t count, CardMask board, Rng& rng) {
  for (size_t i = 0; i < count; ++i) {
    const auto j = i + static_cast<size_t>(uniformBelow(rng, deck.size - i));
    std::swap(deck.cards[i], deck.cards[j]);
    board |= deck.cards[i];
  }
  return board;
}

// Mean and standard error of `n` samples, given their sum and their sum of squares, where
// `unitsPerOne` sample units make 1.0 (kPotUnits for pot shares, 1 for win/tie indicators).
Estimate estimate(uint64_t sum, uint64_t sumOfSquares, uint64_t n, double unitsPerOne) {
  const auto count = static_cast<double>(n);
  const double mean = static_cast<double>(sum) / count;
  if (n < 2) return {mean / unitsPerOne, std::numeric_limits<double>::infinity()};
  // Sample variance: (mean of squares - square of mean), times n / (n - 1) to correct the bias of
  // measuring the spread around the sample's own mean. If every sample is equal, both terms are the
  // same exactly-representable number and the variance is exactly 0.
  const double meanOfSquares = static_cast<double>(sumOfSquares) / count;
  const double variance = std::max(0.0, meanOfSquares - mean * mean) * count / (count - 1);
  return {mean / unitsPerOne, std::sqrt(variance / count) / unitsPerOne};
}

}  // namespace

Estimate MonteCarloResult::win(size_t player) const {
  const uint64_t wins = counts.players[player].wins;  // each trial is 0 or 1, so squares = wins
  return estimate(wins, wins, counts.boards, 1.0);
}

Estimate MonteCarloResult::tie(size_t player) const {
  const uint64_t ties = counts.players[player].ties;
  return estimate(ties, ties, counts.boards, 1.0);
}

Estimate MonteCarloResult::equity(size_t player) const {
  const PlayerResult& p = counts.players[player];
  return estimate(p.wins * kPotUnits + p.tieShares, squaredPotUnits[player], counts.boards,
                  static_cast<double>(kPotUnits));
}

std::variant<MonteCarloResult, EquityError> monteCarloEquity(std::span<const HoleCards> players,
                                                             CardMask board, CardMask dead,
                                                             const MonteCarloOptions& options) {
  if (const auto error = detail::validateDeal(players, board, dead)) return *error;
  if (options.trials == 0) return EquityError::kNoTrials;

  const detail::Seats seats = detail::seatPlayers(players);
  const detail::Deck startingDeck = detail::remainingDeck(board | dead | seats.allHoleCards);
  const size_t missing = detail::missingBoardCards(board);
  const size_t numPlayers = players.size();

  const uint64_t numChunks =
      options.trials / kTrialsPerChunk + (options.trials % kTrialsPerChunk != 0 ? 1 : 0);
  const unsigned threads = detail::threadCount(options.threads, numChunks);

  const auto perThread =
      detail::runInParallel<Totals>(numChunks, threads, [&](uint64_t chunk, Totals& totals) {
        const uint64_t trials = std::min(kTrialsPerChunk, options.trials - chunk * kTrialsPerChunk);
        // The generator lives on the heap, not the stack. Its state is 2.5 KB, and on the stack
        // the hot loop's speed swung by up to 70% with small, unrelated changes to the code
        // around it: a stall most likely caused by its state sharing the low 12 address bits with
        // stack memory written by the calls in the loop ("4K aliasing"). On the heap it's always
        // fast. One allocation per 65,536 trials is negligible.
        const auto rng = std::make_unique<Rng>(mixSeed(options.seed, chunk));
        detail::Deck deck = startingDeck;
        const std::span<PlayerResult> counts(totals.players.data(), numPlayers);
        for (uint64_t trial = 0; trial < trials; ++trial) {
          const detail::PotShares shares =
              detail::showdown(dealRandomBoard(deck, missing, board, *rng), seats.holeMasks());
          detail::addToCounts(shares, counts);
          for (size_t i = 0; i < numPlayers; ++i) {
            totals.squaredPotUnits[i] += shares[i] * shares[i];
          }
        }
        totals.trials += trials;
      });

  MonteCarloResult result;
  result.counts.players.resize(numPlayers);
  result.counts.threads = static_cast<unsigned>(perThread.size());
  result.squaredPotUnits.resize(numPlayers);
  for (const Totals& totals : perThread) {
    detail::addCounts(std::span(totals.players.data(), numPlayers), result.counts.players);
    for (size_t i = 0; i < numPlayers; ++i) {
      result.squaredPotUnits[i] += totals.squaredPotUnits[i];
    }
    result.counts.boards += totals.trials;
  }
  return result;
}

}  // namespace poker
