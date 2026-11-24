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
// and the totals are integers, so adding the chunks up in any order (or, later, on any number of
// threads) gives bit-identical results.
std::variant<MonteCarloResult, EquityError> monteCarloEquity(std::span<const HoleCards> players,
                                                             CardMask board, CardMask dead,
                                                             const MonteCarloOptions& options);

}  // namespace poker
