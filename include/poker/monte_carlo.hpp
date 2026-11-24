#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"

namespace poker {

// An estimated probability together with its uncertainty.
struct Estimate {
  double value = 0;          // the estimate, a fraction in [0, 1]
  double standardError = 0;  // how far the estimate typically is from the true value

  // Half-width of the 95% confidence interval: the true value is within value ± margin95() for 95%
  // of seeds. This uses the normal approximation, which is accurate for many trials but understates
  // the uncertainty for values very close to 0 or 1 (0 wins in 1,000 trials gives a margin of 0).
  double margin95() const { return 1.96 * standardError; }
};

struct MonteCarloOptions {
  uint64_t trials = 0;  // how many random boards to deal
  uint64_t seed = 0;    // the same inputs, trials and seed always give the same result
  // How many threads may share the work: 1 (the default) uses only the calling thread, and 0 means
  // one per hardware thread. Trials are shared out in chunks of 65,536, so there are never more
  // threads than chunks. The result never depends on the number of threads.
  unsigned threads = 1;
};

struct MonteCarloResult {
  EquityResult counts;                    // the same counts as exact enumeration; boards = trials
  std::vector<uint64_t> squaredPotUnits;  // per player: sum over trials of (pot units won)^2

  // Estimates with standard errors. With fewer than 2 trials the spread can't be estimated, and the
  // standard error is infinite.
  Estimate win(size_t player) const;
  Estimate tie(size_t player) const;
  Estimate equity(size_t player) const;
};

// Estimates all-in equity by dealing `options.trials` random completions of the board (from the
// cards nobody holds, excluding `dead`) and scoring each one exactly as exactEquity() does.
//
// The result depends only on the inputs, the number of trials and the seed, not on how the work is
// split up: trials run in fixed-size chunks, each with its own random stream derived from the seed,
// and the totals are integers, so adding the chunks up in any order, on any number of threads,
// gives bit-identical results.
std::variant<MonteCarloResult, EquityError> monteCarloEquity(std::span<const HoleCards> players,
                                                             CardMask board, CardMask dead,
                                                             const MonteCarloOptions& options);

// The same for players holding ranges. Each trial first deals every player a hand from their range
// (hands that use a board or dead card are left out), giving every valid combination of hands the
// same chance, then deals the rest of the board. A known hand is a range with one hand: it uses no
// random numbers, so known hands give exactly the same results as the HoleCards version.
std::variant<MonteCarloResult, EquityError> monteCarloEquity(std::span<const Range> players,
                                                             CardMask board, CardMask dead,
                                                             const MonteCarloOptions& options);

}  // namespace poker
