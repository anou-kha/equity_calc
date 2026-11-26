// The JavaScript API of the browser module, made with Emscripten's embind:
//
//   import createPokerModule from "./poker_wasm.js";
//   const poker = await createPokerModule();
//   const result = JSON.parse(poker.calculate({ranges: ["QQ+, AKs", "random"], board: "Js Ts
//   2h"})); poker.version();  // e.g. "1.0.0"
//
// calculate() takes one object. `ranges` (an array of strings, one per player) is required; the
// rest are optional, with the same defaults as CalculationRequest:
//   board, dead   strings of cards, e.g. "Js Ts 2h"
//   method        "auto", "exact" or "montecarlo"
//   exactLimit    the most showdowns "auto" enumerates exactly
//   trials, seed  Monte Carlo settings
// It returns the same JSON as `poker_cli --json`, made by the same code (calculator.hpp), so the
// page and the CLI always agree. A request of the wrong shape is reported in the same JSON form,
// as a "setup" error.
//
// The module is single-threaded (see CMakeLists.txt), so a long calculation blocks the thread it
// runs on: the web page runs it in a Web Worker, which keeps the page itself responsive.
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "poker/calculator.hpp"

namespace {

using emscripten::val;

// JavaScript numbers are doubles, which hold every whole number up to 2^53 exactly.
constexpr double kMaxExactInteger = 9007199254740992.0;

std::string requestError(const std::string& message) {
  return poker::toJson(
      poker::CalculationError{poker::CalculationError::Field::kSetup, 0, "request: " + message});
}

bool isMissing(const val& value) { return value.isUndefined() || value.isNull(); }

// A string property: "" if it's missing, std::nullopt if it isn't a string.
std::optional<std::string> stringField(const val& request, const char* name) {
  const val value = request[name];
  if (isMissing(value)) return std::string();
  if (!value.isString()) return std::nullopt;
  return value.as<std::string>();
}

// A whole-number property: `fallback` if it's missing, std::nullopt if it isn't a whole number
// from 0 to 2^53.
std::optional<uint64_t> numberField(const val& request, const char* name, uint64_t fallback) {
  const val value = request[name];
  if (isMissing(value)) return fallback;
  if (!value.isNumber()) return std::nullopt;
  const double number = value.as<double>();
  // Written so that NaN (for which every comparison is false) is rejected too.
  if (!(number >= 0 && number <= kMaxExactInteger) || std::floor(number) != number) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(number);
}

std::optional<poker::Method> parseMethod(const std::string& text) {
  if (text.empty() || text == "auto") return poker::Method::kAuto;
  if (text == "exact") return poker::Method::kExact;
  if (text == "montecarlo") return poker::Method::kMonteCarlo;
  return std::nullopt;
}

std::string calculate(const val& request) {
  if (isMissing(request) || request.typeOf().as<std::string>() != "object") {
    return requestError("calculate() takes an object, e.g. {ranges: [\"AA\", \"KK\"]}");
  }
  poker::CalculationRequest input;

  const val ranges = request["ranges"];
  if (!val::global("Array").call<bool>("isArray", ranges)) {
    return requestError("ranges must be an array of strings");
  }
  const auto count = ranges["length"].as<unsigned>();
  for (unsigned i = 0; i < count; ++i) {
    const val range = ranges[i];
    if (!range.isString()) return requestError("ranges must be an array of strings");
    input.ranges.push_back(range.as<std::string>());
  }

  const auto board = stringField(request, "board");
  const auto dead = stringField(request, "dead");
  const auto methodText = stringField(request, "method");
  if (!board || !dead || !methodText) {
    return requestError("board, dead and method must be strings");
  }
  const auto method = parseMethod(*methodText);
  if (!method) return requestError("method must be \"auto\", \"exact\" or \"montecarlo\"");
  const auto exactLimit = numberField(request, "exactLimit", input.exactLimit);
  const auto trials = numberField(request, "trials", input.trials);
  const auto seed = numberField(request, "seed", input.seed);
  if (!exactLimit || !trials || !seed) {
    return requestError("exactLimit, trials and seed must be whole numbers from 0 to 2^53");
  }

  input.board = *board;
  input.dead = *dead;
  input.method = *method;
  input.exactLimit = *exactLimit;
  input.trials = *trials;
  input.seed = *seed;
  input.threads = 1;  // this build has no threads

  const auto outcome = poker::calculate(input);
  return std::visit([](const auto& result) { return poker::toJson(result); }, outcome);
}

std::string version() { return std::string(poker::version()); }

}  // namespace

EMSCRIPTEN_BINDINGS(poker) {
  emscripten::function("calculate", &calculate);
  emscripten::function("version", &version);
}
