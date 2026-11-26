// The application layer shared by the command-line tool and the WebAssembly build: read text input,
// choose between exact enumeration and Monte Carlo, run it, and describe the result, as a struct or
// as JSON.
#include "poker/calculator.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"
#include "poker/hand_ranking.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/range.hpp"

namespace poker {
namespace {

using Clock = std::chrono::steady_clock;
using Field = CalculationError::Field;

CalculationError makeError(Field field, std::string message, size_t player = 0) {
  return CalculationError{field, player, std::move(message)};
}

// 'invalid range "QQ+, AKx" at "AKx": the last letter must be ...' (the entry only if it isn't
// the whole range).
std::string rangeErrorMessage(std::string_view text, const RangeError& error) {
  std::string message = "invalid range \"" + std::string(text) + "\"";
  if (error.token != text) message += " at \"" + error.token + "\"";
  return message + ": " + std::string(error.reason);
}

std::string cardsToString(const std::vector<Card>& cards) {
  std::string text;
  for (const Card card : cards) {
    if (!text.empty()) text += ' ';
    text += card.toString();
  }
  return text;
}

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

// ---- JSON writing -------------------------------------------------------------------------------

// Appends `text` as a JSON string: quotes, backslashes and control characters are escaped, so any
// user input (like a mistyped range) produces valid JSON.
void appendString(std::string& out, std::string_view text) {
  constexpr std::string_view kHex = "0123456789abcdef";
  out += '"';
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (byte < 0x20) {
      out += "\\u00";
      out += kHex[byte >> 4];
      out += kHex[byte & 0xF];
    } else {
      out += c;
    }
  }
  out += '"';
}

// The shortest text that reads back as exactly the same number (std::to_chars), e.g. 0.25 or
// 0.8194614869. JSON has no infinity, so a margin that can't be estimated is written as null.
void appendNumber(std::string& out, double value) {
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
  out.append(buffer, result.ptr);
}

void appendNumber(std::string& out, uint64_t value) {
  char buffer[24];
  const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
  out.append(buffer, result.ptr);
}

// A number, or null where it doesn't apply (e.g. the seed in an exact report).
template <typename Number>
void appendNumberOrNull(std::string& out, bool applies, Number value) {
  if (applies) {
    appendNumber(out, value);
  } else {
    out += "null";
  }
}

void appendKey(std::string& out, std::string_view key) {
  if (out.back() != '{') out += ',';
  appendString(out, key);
  out += ':';
}

std::string_view methodName(Method method) {
  switch (method) {
    case Method::kAuto:
      return "auto";
    case Method::kExact:
      return "exact";
    case Method::kMonteCarlo:
      return "montecarlo";
  }
  return "unknown";
}

std::string_view fieldName(Field field) {
  switch (field) {
    case Field::kRange:
      return "range";
    case Field::kBoard:
      return "board";
    case Field::kDead:
      return "dead";
    case Field::kSetup:
      return "setup";
  }
  return "unknown";
}

}  // namespace

#ifndef POKER_VERSION
#define POKER_VERSION "unknown"  // CMake defines it from the project version
#endif

std::string_view version() { return POKER_VERSION; }

std::variant<CalculationReport, CalculationError> calculate(const CalculationRequest& request) {
  std::vector<Range> ranges;
  for (size_t player = 0; player < request.ranges.size(); ++player) {
    const std::string& text = request.ranges[player];
    const auto parsed = parseRange(text);
    if (const auto* rangeError = std::get_if<RangeError>(&parsed)) {
      return makeError(Field::kRange, rangeErrorMessage(text, *rangeError), player);
    }
    ranges.push_back(std::get<Range>(parsed));
  }
  const std::optional<std::vector<Card>> board = parseCards(request.board);
  if (!board) return makeError(Field::kBoard, "invalid board cards: " + request.board);
  const std::optional<std::vector<Card>> dead = parseCards(request.dead);
  if (!dead) return makeError(Field::kDead, "invalid dead cards: " + request.dead);
  const CardMask boardMask = toMask(*board);
  const CardMask deadMask = toMask(*dead);

  CalculationReport report;
  report.board = cardsToString(*board);
  report.dead = cardsToString(*dead);
  report.knownHands = std::all_of(ranges.begin(), ranges.end(),
                                  [](const Range& range) { return range.size() == 1; });
  report.method = request.method;
  if (request.method == Method::kAuto) {
    const bool tooBig =
        exactShowdowns(ranges, boardMask, deadMask, request.exactLimit) > request.exactLimit;
    report.method = tooBig ? Method::kMonteCarlo : Method::kExact;
    report.automatic = tooBig;
  }

  buildEvaluatorTables();  // a few ms, once, outside the timed part
  const auto start = Clock::now();
  if (report.method == Method::kExact) {
    const auto outcome = exactEquity(ranges, boardMask, deadMask, {.threads = request.threads});
    report.seconds = secondsSince(start);
    if (const auto* equityError = std::get_if<EquityError>(&outcome)) {
      return makeError(Field::kSetup, std::string(toString(*equityError)));
    }
    const EquityResult& result = std::get<EquityResult>(outcome);
    for (size_t i = 0; i < ranges.size(); ++i) {
      report.players.push_back({request.ranges[i], ranges[i].size(), result.win(i), result.tie(i),
                                result.equity(i), 0.0});
    }
    report.showdowns = result.boards;
    report.deals = result.deals;
    report.threads = result.threads;
  } else {
    const auto outcome = monteCarloEquity(
        ranges, boardMask, deadMask,
        {.trials = request.trials, .seed = request.seed, .threads = request.threads});
    report.seconds = secondsSince(start);
    if (const auto* equityError = std::get_if<EquityError>(&outcome)) {
      return makeError(Field::kSetup, std::string(toString(*equityError)));
    }
    const MonteCarloResult& result = std::get<MonteCarloResult>(outcome);
    for (size_t i = 0; i < ranges.size(); ++i) {
      report.players.push_back({request.ranges[i], ranges[i].size(), result.counts.win(i),
                                result.counts.tie(i), result.counts.equity(i),
                                result.equity(i).margin95()});
    }
    report.showdowns = result.counts.boards;
    report.seed = request.seed;
    report.threads = result.counts.threads;
  }
  return report;
}

std::string toJson(const CalculationReport& report) {
  const bool exact = report.method == Method::kExact;
  std::string out = "{";
  appendKey(out, "ok");
  out += "true";
  appendKey(out, "method");
  appendString(out, methodName(report.method));
  appendKey(out, "automatic");
  out += report.automatic ? "true" : "false";
  appendKey(out, "board");
  appendString(out, report.board);
  appendKey(out, "dead");
  appendString(out, report.dead);
  appendKey(out, "knownHands");
  out += report.knownHands ? "true" : "false";
  appendKey(out, "showdowns");
  appendNumber(out, report.showdowns);
  appendKey(out, "deals");
  appendNumberOrNull(out, exact, report.deals);
  appendKey(out, "seed");
  appendNumberOrNull(out, !exact, report.seed);
  appendKey(out, "threads");
  appendNumber(out, uint64_t{report.threads});
  appendKey(out, "milliseconds");
  appendNumber(out, report.seconds * 1000.0);
  appendKey(out, "players");
  out += '[';
  for (const PlayerReport& player : report.players) {
    if (out.back() != '[') out += ',';
    out += '{';
    appendKey(out, "range");
    appendString(out, player.range);
    appendKey(out, "hands");
    appendNumber(out, uint64_t{player.hands});
    appendKey(out, "win");
    appendNumber(out, player.win);
    appendKey(out, "tie");
    appendNumber(out, player.tie);
    appendKey(out, "equity");
    appendNumber(out, player.equity);
    appendKey(out, "margin95");
    appendNumberOrNull(out, !exact, player.margin95);
    out += '}';
  }
  out += "]}";
  return out;
}

std::string toJson(const CalculationError& error) {
  std::string out = "{";
  appendKey(out, "ok");
  out += "false";
  appendKey(out, "field");
  appendString(out, fieldName(error.field));
  appendKey(out, "player");
  appendNumberOrNull(out, error.field == Field::kRange, uint64_t{error.player});
  appendKey(out, "message");
  appendString(out, error.message);
  out += '}';
  return out;
}

std::string rangeJson(std::string_view text) {
  const auto parsed = parseRange(text);
  if (const auto* error = std::get_if<RangeError>(&parsed)) {
    std::string out = "{";
    appendKey(out, "ok");
    out += "false";
    appendKey(out, "message");
    appendString(out, rangeErrorMessage(text, *error));
    out += '}';
    return out;
  }
  return rangeJson(std::get<Range>(parsed));
}

std::string cardsJson(std::string_view text) {
  const std::optional<std::vector<Card>> cards = parseCards(text);
  std::string out = "{";
  appendKey(out, "ok");
  if (!cards) {
    out += "false";
    appendKey(out, "message");
    appendString(out, "invalid cards: " + std::string(text));
    out += '}';
    return out;
  }
  out += "true";
  appendKey(out, "text");
  appendString(out, cardsToString(*cards));
  appendKey(out, "cards");
  out += '[';
  for (const Card card : *cards) {
    if (out.back() != '[') out += ',';
    appendNumber(out, uint64_t{static_cast<unsigned>(card.index())});
  }
  out += "]}";
  return out;
}

std::string handRankingJson() {
  std::string out = "[";
  for (const RankedClass& handClass : handRanking()) {
    if (out.back() != '[') out += ',';
    out += '{';
    appendKey(out, "name");
    appendString(out, handClass.name);
    appendKey(out, "equity");
    appendNumber(out, handClass.equity());
    out += '}';
  }
  out += ']';
  return out;
}

std::string rangeJson(const Range& range) {
  std::string out = "{";
  appendKey(out, "ok");
  out += "true";
  appendKey(out, "text");
  appendString(out, formatRange(range));
  appendKey(out, "hands");
  out += '[';
  for (const HoleCards& hand : range.combos()) {  // each with its higher card first
    if (out.back() != '[') out += ',';
    out += '[';
    appendNumber(out, uint64_t{static_cast<unsigned>(hand[0].index())});
    out += ',';
    appendNumber(out, uint64_t{static_cast<unsigned>(hand[1].index())});
    out += ']';
  }
  out += "]}";
  return out;
}

}  // namespace poker
