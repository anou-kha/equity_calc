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
//
// `excluded` holds deck cards that are in a player's hand this trial (players dealt from a range).
// If one is picked, it's rejected and another card drawn, which keeps the choice uniform among the
// cards that are really free. With nothing excluded (known hands), each card takes exactly one
// random number.
CardMask dealRandomBoard(detail::Deck& deck, size_t count, CardMask board, CardMask excluded,
                         Rng& rng) {
  for (size_t i = 0; i < count; ++i) {
    do {
      const auto j = i + static_cast<size_t>(uniformBelow(rng, deck.size - i));
      std::swap(deck.cards[i], deck.cards[j]);
    } while (deck.cards[i] & excluded);
    board |= deck.cards[i];
  }
  return board;
}

// Deals each player in `sampled` one of their live hands, with no card used twice, by rejection
// sampling: draw every hand independently, and if two hands collide (or a hand collides with a
// known player's hand), throw the whole draw away and start again. Every valid combination of
// hands is then exactly equally likely. (Stopping at the first collision instead of drawing the
// rest changes nothing: that draw would be thrown away anyway.)
//
// The tempting shortcut, drawing each player's hand only from those that don't collide with the
// hands already drawn, is biased: a hand that collides with most of the next player's hands is
// still drawn as often as any other, although it belongs to fewer valid combinations. A test
// checks this case. Returns the cards of the hands drawn.
CardMask dealHands(const detail::LiveHands& hands, std::span<const size_t> sampled,
                   CardMask knownHoleCards, Rng& rng, detail::Seats& seats) {
  while (true) {
    CardMask used = knownHoleCards;
    bool collided = false;
    for (const size_t player : sampled) {
      const std::vector<CardMask>& live = hands[player];
      const CardMask hand = live[static_cast<size_t>(uniformBelow(rng, live.size()))];
      if (hand & used) {
        collided = true;
        break;
      }
      used |= hand;
      seats.holes[player] = hand;
    }
    if (!collided) return used & ~knownHoleCards;
  }
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
  // Validate known hands with their own, more specific errors, then treat each as a range of one.
  if (const auto error = detail::validateDeal(players, board, dead)) return *error;
  return monteCarloEquity(detail::singleHandRanges(players), board, dead, options);
}

std::variant<MonteCarloResult, EquityError> monteCarloEquity(std::span<const Range> players,
                                                             CardMask board, CardMask dead,
                                                             const MonteCarloOptions& options) {
  const auto prepared = detail::liveHands(players, board, dead);
  if (const auto* error = std::get_if<EquityError>(&prepared)) return *error;
  if (options.trials == 0) return EquityError::kNoTrials;
  const auto& hands = std::get<detail::LiveHands>(prepared);
  // Rejection sampling only ever finishes if a valid deal exists, so check that there is one.
  if (detail::forEachDeal(hands, [](const detail::Seats&) { return false; })) {
    return detail::noValidDealError(players);
  }

  // Players with only one possible hand get it in every trial, and their cards stay out of the
  // deck; the others are dealt a hand from their range in each trial.
  const size_t numPlayers = players.size();
  detail::Seats knownSeats;
  knownSeats.count = numPlayers;
  CardMask knownHoleCards = 0;
  std::vector<size_t> sampled;
  for (size_t player = 0; player < numPlayers; ++player) {
    if (hands[player].size() == 1) {
      knownSeats.holes[player] = hands[player][0];
      knownHoleCards |= hands[player][0];
    } else {
      sampled.push_back(player);
    }
  }
  const detail::Deck startingDeck = detail::remainingDeck(board | dead | knownHoleCards);
  const size_t missing = detail::missingBoardCards(board);

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
          detail::Seats seats = knownSeats;
          const CardMask dealtHoleCards = dealHands(hands, sampled, knownHoleCards, *rng, seats);
          const CardMask fullBoard = dealRandomBoard(deck, missing, board, dealtHoleCards, *rng);
          const detail::PotShares shares = detail::showdown(fullBoard, seats.holeMasks());
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
