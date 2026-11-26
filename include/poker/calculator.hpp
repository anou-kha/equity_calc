#pragma once

// The application layer: turns text input ("QQ+, AKs", "Js Ts 2h", ...) into a finished report,
// choosing between exact enumeration and Monte Carlo. The command-line tool and the WebAssembly
// build both use it, so they always agree, down to the JSON they produce.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "poker/range.hpp"

namespace poker {

// The project version from CMakeLists.txt, e.g. "1.0.0".
std::string_view version();

enum class Method {
  kAuto,        // exact if that takes at most `exactLimit` showdowns, otherwise Monte Carlo
  kExact,       // always enumerate every deal and board
  kMonteCarlo,  // always sample
};

// The default for the most showdowns kAuto will enumerate exactly: about a second on 8 cores.
inline constexpr uint64_t kAutoExactLimit = 200'000'000;

struct CalculationRequest {
  std::vector<std::string> ranges;  // one per player, in range notation: "QQ+, AKs", "AsKs", ...
  std::string board;                // 0, 3, 4 or 5 cards, e.g. "Js Ts 2h" (spaces optional)
  std::string dead;                 // cards nobody can be dealt
  Method method = Method::kAuto;
  uint64_t exactLimit = kAutoExactLimit;  // kAuto only; lower it for slower (e.g. 1-thread) runs
  uint64_t trials = 10'000'000;           // Monte Carlo only
  uint64_t seed = 1;                      // Monte Carlo only
  unsigned threads = 1;                   // 0 means one per hardware thread
};

struct PlayerReport {
  std::string range;  // as given
  size_t hands = 0;   // hands in the range
  double win = 0;     // fractions in [0, 1]
  double tie = 0;
  double equity = 0;
  double margin95 = 0;  // Monte Carlo only: half-width of the 95% confidence interval for equity
};

struct CalculationReport {
  Method method = Method::kExact;  // the method actually used: kExact or kMonteCarlo
  bool automatic = false;          // Monte Carlo was chosen because exact would take too long
  std::string board;               // normalised, e.g. "Js Ts 2h"
  std::string dead;
  std::vector<PlayerReport> players;
  bool knownHands = false;  // every player holds a single, known hand
  uint64_t showdowns = 0;   // exact: deals x boards; Monte Carlo: trials
  uint64_t deals = 0;       // exact only: ways of dealing the players' hands
  uint64_t seed = 0;        // Monte Carlo only
  unsigned threads = 1;     // threads that did the work
  double seconds = 0;       // time for the equity calculation itself
};

struct CalculationError {
  enum class Field {
    kRange,  // a player's range couldn't be read; `player` says whose
    kBoard,  // the board couldn't be read
    kDead,   // the dead cards couldn't be read
    kSetup,  // the inputs don't fit together (too few players, a card used twice, ...)
  };
  Field field = Field::kSetup;
  size_t player = 0;  // for kRange: the player's index, from 0
  std::string message;
};

std::variant<CalculationReport, CalculationError> calculate(const CalculationRequest& request);

// JSON for programs: {"ok":true, ...} for a report and {"ok":false, ...} for an error.
std::string toJson(const CalculationReport& report);
std::string toJson(const CalculationError& error);

// For range pickers: a range as JSON, {"ok":true,"text":"QQ+, AKs","hands":[[51,47],...]}, with
// its standard notation (formatRange) and every hand as its two card indices, higher first, in
// canonical order. Text that can't be read gives {"ok":false,"message":"invalid range ..."}, the
// same message calculate() gives.
std::string rangeJson(std::string_view text);
std::string rangeJson(const Range& range);

// For card pickers: cards like "jsTs2h" as JSON, {"ok":true,"text":"Js Ts 2h","cards":[39,35,2]},
// in the order given, or {"ok":false,"message":"invalid cards: ..."} if the text can't be read (a
// card given twice included). No cards at all is fine.
std::string cardsJson(std::string_view text);

}  // namespace poker
