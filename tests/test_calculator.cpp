// The application layer shared by the CLI and the WebAssembly build: text in, report (or JSON) out.
// The equity numbers themselves are tested elsewhere; these tests check that calculate() reads its
// input correctly, picks the right method, passes the library's results through unchanged, and
// reports errors with the right field. The JSON tests use hand-built reports, so they don't depend
// on timings.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "poker/calculator.hpp"
#include "poker/equity.hpp"
#include "poker/hand_ranking.hpp"
#include "poker/monte_carlo.hpp"
#include "poker/range.hpp"
#include "test_helpers.hpp"

namespace {

using poker::CalculationError;
using poker::CalculationReport;
using poker::CalculationRequest;
using poker::Method;
using poker::test::cards;
using Field = poker::CalculationError::Field;

CalculationRequest request(std::initializer_list<std::string> ranges, std::string board = "",
                           Method method = Method::kAuto) {
  CalculationRequest result;
  result.ranges = ranges;
  result.board = std::move(board);
  result.method = method;
  return result;
}

CalculationReport report(const CalculationRequest& input) {
  auto outcome = poker::calculate(input);
  if (const auto* error = std::get_if<CalculationError>(&outcome)) {
    ADD_FAILURE() << "unexpected error: " << error->message;
    return {};
  }
  return std::get<CalculationReport>(outcome);
}

CalculationError error(const CalculationRequest& input) {
  auto outcome = poker::calculate(input);
  if (std::holds_alternative<CalculationReport>(outcome)) {
    ADD_FAILURE() << "expected an error";
    return {};
  }
  return std::get<CalculationError>(outcome);
}

std::vector<poker::Range> parsed(std::initializer_list<std::string_view> texts) {
  std::vector<poker::Range> result;
  for (const std::string_view text : texts)
    result.push_back(std::get<poker::Range>(poker::parseRange(text)));
  return result;
}

// ---- Choosing the method ------------------------------------------------------------------------

TEST(Calculator, SmallSpotsAreEnumeratedExactly) {
  const CalculationReport result = report(request({"AsKs", "QhQd"}, "Js Ts 2h"));
  EXPECT_EQ(result.method, Method::kExact);
  EXPECT_FALSE(result.automatic);
  EXPECT_TRUE(result.knownHands);
  EXPECT_EQ(result.showdowns, 990u);  // C(45, 2) turns and rivers
  EXPECT_EQ(result.deals, 1u);
  EXPECT_EQ(result.threads, 1u);  // the library default
  EXPECT_GE(result.seconds, 0.0);
}

TEST(Calculator, LargeSpotsSwitchToMonteCarlo) {
  CalculationRequest input = request({"random", "random", "random"});
  input.trials = 20'000;
  input.seed = 9;
  const CalculationReport result = report(input);
  EXPECT_EQ(result.method, Method::kMonteCarlo);
  EXPECT_TRUE(result.automatic);
  EXPECT_FALSE(result.knownHands);
  EXPECT_EQ(result.showdowns, 20'000u);
  EXPECT_EQ(result.seed, 9u);
  for (const poker::PlayerReport& player : result.players) {
    EXPECT_EQ(player.hands, 1326u);
    EXPECT_GT(player.margin95, 0.0);
  }
}

TEST(Calculator, ExactLimitCanBeLowered) {
  // 990 showdowns: exact up to a limit of 990, Monte Carlo below it.
  CalculationRequest input = request({"AsKs", "QhQd"}, "Js Ts 2h");
  input.trials = 1000;
  input.exactLimit = 990;
  EXPECT_EQ(report(input).method, Method::kExact);
  input.exactLimit = 989;
  const CalculationReport sampled = report(input);
  EXPECT_EQ(sampled.method, Method::kMonteCarlo);
  EXPECT_TRUE(sampled.automatic);
}

// Three random hands on the river: 966 million deals of one board each. Even with a limit that
// would allow that many showdowns, "auto" must not choose exact enumeration, which refuses more
// than kMaxExactDeals deals.
TEST(Calculator, TooManyDealsMeansMonteCarlo) {
  CalculationRequest input = request({"random", "random", "random"}, "2c 7d 9h Js Kh");
  input.exactLimit = uint64_t{1} << 50;
  input.trials = 1000;
  const CalculationReport sampled = report(input);
  EXPECT_EQ(sampled.method, Method::kMonteCarlo);
  EXPECT_TRUE(sampled.automatic);

  input.method = Method::kExact;
  const CalculationError refused = error(input);
  EXPECT_EQ(refused.field, Field::kSetup);
  EXPECT_EQ(refused.message, poker::toString(poker::EquityError::kTooManyDeals));
}

TEST(Calculator, MethodCanBeForced) {
  CalculationRequest monteCarlo = request({"AsKs", "QhQd"}, "Js Ts 2h", Method::kMonteCarlo);
  monteCarlo.trials = 1000;
  const CalculationReport sampled = report(monteCarlo);
  EXPECT_EQ(sampled.method, Method::kMonteCarlo);
  EXPECT_FALSE(sampled.automatic);  // asked for, not chosen
  EXPECT_EQ(sampled.showdowns, 1000u);

  const CalculationReport enumerated = report(request({"AA", "KK"}, "2c 7d 9h", Method::kExact));
  EXPECT_EQ(enumerated.method, Method::kExact);
  EXPECT_EQ(enumerated.deals, 36u);
  EXPECT_EQ(enumerated.showdowns, 36u * 990);
}

// ---- Results are the library's, unchanged -------------------------------------------------------

TEST(Calculator, ExactResultsAreTheLibrarys) {
  const CalculationReport result = report(request({"QQ+, AKs", "JJ-99"}, "Ah 7d 2c"));
  const auto expected = poker::exactEquity(parsed({"QQ+, AKs", "JJ-99"}), cards("Ah 7d 2c"));
  const auto& counts = std::get<poker::EquityResult>(expected);
  ASSERT_EQ(result.players.size(), 2u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(result.players[i].win, counts.win(i));
    EXPECT_EQ(result.players[i].tie, counts.tie(i));
    EXPECT_EQ(result.players[i].equity, counts.equity(i));
    EXPECT_EQ(result.players[i].margin95, 0.0);
  }
  EXPECT_EQ(result.players[0].range, "QQ+, AKs");  // as typed
  EXPECT_EQ(result.players[0].hands, 22u);         // 18 pairs + 4 suited AK
  EXPECT_EQ(result.players[1].hands, 18u);
  EXPECT_EQ(result.showdowns, counts.boards);
  EXPECT_EQ(result.deals, counts.deals);
}

TEST(Calculator, MonteCarloResultsAreTheLibrarys) {
  CalculationRequest input = request({"AsKs", "QhQd", "7c7d"}, "", Method::kMonteCarlo);
  input.trials = 50'000;
  input.seed = 3;
  const CalculationReport result = report(input);
  const auto expected = poker::monteCarloEquity(parsed({"AsKs", "QhQd", "7c7d"}), 0, 0,
                                                {.trials = 50'000, .seed = 3});
  const auto& sampled = std::get<poker::MonteCarloResult>(expected);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(result.players[i].equity, sampled.counts.equity(i));
    EXPECT_EQ(result.players[i].margin95, sampled.equity(i).margin95());
  }
}

TEST(Calculator, ThreadCountNeverChangesTheResult) {
  CalculationRequest input = request({"QQ+", "AKs, AKo"}, "", Method::kMonteCarlo);
  input.trials = 200'000;
  const CalculationReport oneThread = report(input);
  input.threads = 4;
  const CalculationReport fourThreads = report(input);
  EXPECT_EQ(fourThreads.threads, 4u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(oneThread.players[i].win, fourThreads.players[i].win);
    EXPECT_EQ(oneThread.players[i].tie, fourThreads.players[i].tie);
  }
}

TEST(Calculator, CardsAreNormalised) {
  CalculationRequest input = request({"AsKs", "QhQd"}, "jsTS2h");
  input.dead = " 9c ";
  const CalculationReport result = report(input);
  EXPECT_EQ(result.board, "Js Ts 2h");
  EXPECT_EQ(result.dead, "9c");
  EXPECT_EQ(result.showdowns, 946u);  // C(44, 2): the dead card is out of the deck
}

// ---- Errors -------------------------------------------------------------------------------------

TEST(Calculator, RangeErrorsSayWhoseRangeAndWhere) {
  const CalculationError bad = error(request({"AsKs", "QQ+, AKx"}));
  EXPECT_EQ(bad.field, Field::kRange);
  EXPECT_EQ(bad.player, 1u);
  EXPECT_NE(bad.message.find("\"QQ+, AKx\""), std::string::npos) << bad.message;
  EXPECT_NE(bad.message.find("at \"AKx\""), std::string::npos) << bad.message;

  // When the whole range is the bad entry, it isn't repeated.
  const CalculationError whole = error(request({"AKx", "QQ"}));
  EXPECT_EQ(whole.player, 0u);
  EXPECT_EQ(whole.message.find(" at "), std::string::npos) << whole.message;
}

TEST(Calculator, CardErrorsSayWhichField) {
  EXPECT_EQ(error(request({"AsKs", "QhQd"}, "Js Ts 2x")).field, Field::kBoard);
  CalculationRequest input = request({"AsKs", "QhQd"});
  input.dead = "9c9c";
  EXPECT_EQ(error(input).field, Field::kDead);
}

TEST(Calculator, SetupErrorsUseTheLibrarysMessages) {
  const CalculationError onePlayer = error(request({"AsKs"}));
  EXPECT_EQ(onePlayer.field, Field::kSetup);
  EXPECT_EQ(onePlayer.message, poker::toString(poker::EquityError::kTooFewPlayers));

  // Known hands get the same error with either method.
  for (const Method method : {Method::kAuto, Method::kExact, Method::kMonteCarlo}) {
    const CalculationError shared = error(request({"AsKs", "AsQd"}, "", method));
    EXPECT_EQ(shared.field, Field::kSetup);
    EXPECT_EQ(shared.message, poker::toString(poker::EquityError::kDuplicateCard));
  }
  EXPECT_EQ(error(request({"AsKs", "QhQd"}, "Js Ts")).message,
            poker::toString(poker::EquityError::kBadBoardSize));
}

// ---- JSON ---------------------------------------------------------------------------------------

TEST(CalculatorJson, ExactReport) {
  CalculationReport result;
  result.method = Method::kExact;
  result.board = "Js Ts 2h";
  result.knownHands = true;
  result.showdowns = 990;
  result.deals = 1;
  result.threads = 2;
  result.seconds = 0.015625;  // exact in binary, so the JSON is exactly 15.625
  result.players = {{"AsKs", 1, 0.5, 0.25, 0.625, 0.0}, {"QhQd", 1, 0.25, 0.25, 0.375, 0.0}};
  EXPECT_EQ(poker::toJson(result),
            R"({"ok":true,"method":"exact","automatic":false,"board":"Js Ts 2h","dead":"",)"
            R"("knownHands":true,"showdowns":990,"deals":1,"seed":null,"threads":2,)"
            R"("milliseconds":15.625,"players":[)"
            R"({"range":"AsKs","hands":1,"win":0.5,"tie":0.25,"equity":0.625,"margin95":null},)"
            R"({"range":"QhQd","hands":1,"win":0.25,"tie":0.25,"equity":0.375,"margin95":null}]})");
}

TEST(CalculatorJson, MonteCarloReport) {
  CalculationReport result;
  result.method = Method::kMonteCarlo;
  result.automatic = true;
  result.showdowns = 1000;
  result.seed = 7;
  result.players = {{"random", 1326, 0.5, 0, 0.5, 0.03125},
                    {"random", 1326, 0.5, 0, 0.5, std::numeric_limits<double>::infinity()}};
  EXPECT_EQ(poker::toJson(result),
            R"({"ok":true,"method":"montecarlo","automatic":true,"board":"","dead":"",)"
            R"("knownHands":false,"showdowns":1000,"deals":null,"seed":7,"threads":1,)"
            R"("milliseconds":0,"players":[)"
            R"({"range":"random","hands":1326,"win":0.5,"tie":0,"equity":0.5,"margin95":0.03125},)"
            // JSON has no infinity: a margin that can't be estimated is null.
            R"({"range":"random","hands":1326,"win":0.5,"tie":0,"equity":0.5,"margin95":null}]})");
}

TEST(CalculatorJson, NumbersReadBackExactly) {
  CalculationReport result;
  result.players = {{"AA", 6, 0.1, 0, 1.0 / 3.0, 0}};
  const std::string json = poker::toJson(result);
  EXPECT_NE(json.find(R"("win":0.1,)"), std::string::npos) << json;  // shortest form, not 0.1000...
  EXPECT_NE(json.find(R"("equity":0.3333333333333333,)"), std::string::npos) << json;
}

TEST(CalculatorJson, ErrorsEscapeUserInput) {
  const CalculationError bad{Field::kRange, 2, "invalid range \"A\\K\"\n\t\x01"};
  EXPECT_EQ(poker::toJson(bad), R"({"ok":false,"field":"range","player":2,)"
                                R"("message":"invalid range \"A\\K\"\u000a\u0009\u0001"})");
  const CalculationError setup{Field::kSetup, 0, "at least 2 players are needed"};
  EXPECT_EQ(
      poker::toJson(setup),
      R"({"ok":false,"field":"setup","player":null,"message":"at least 2 players are needed"})");
}

// ---- Range pickers ------------------------------------------------------------------------------

TEST(CalculatorRanges, RangeJsonHasTheTextAndEveryHand) {
  // AcKc, AdKd, AhKh, AsKs: card index = rank * 4 + suit, the higher card first.
  EXPECT_EQ(poker::rangeJson("AKs"),
            R"({"ok":true,"text":"AKs","hands":[[48,44],[49,45],[50,46],[51,47]]})");
  EXPECT_EQ(poker::rangeJson("KhAh"), R"({"ok":true,"text":"AhKh","hands":[[50,46]]})");
  EXPECT_EQ(poker::rangeJson(poker::Range{}), R"({"ok":true,"text":"","hands":[]})");

  const std::string all = poker::rangeJson(" any ");
  EXPECT_NE(all.find(R"("text":"random")"), std::string::npos);
  EXPECT_EQ(std::count(all.begin(), all.end(), '[') - 1, 1326);  // one [ per hand, plus the list's
}

TEST(CalculatorRanges, UnreadableRangesGiveTheSameMessageAsCalculate) {
  const std::string json = poker::rangeJson("QQ+, AKx");
  const CalculationError fromCalculate = error(request({"AsKs", "QQ+, AKx"}));
  // (A custom raw-string delimiter, because the text itself contains `)"`.)
  EXPECT_EQ(json, R"json({"ok":false,"message":"invalid range \"QQ+, AKx\" at \"AKx\": )json"
                  R"json(the last letter must be 's' (suited) or 'o' (offsuit)"})json");
  EXPECT_EQ(fromCalculate.message,
            "invalid range \"QQ+, AKx\" at \"AKx\": the last letter must be 's' (suited) or 'o' "
            "(offsuit)");
}

TEST(CalculatorRanges, HandRankingJsonListsEveryClassBestFirst) {
  const std::string json = poker::handRankingJson();
  EXPECT_EQ(json.rfind(R"([{"name":"AA","equity":0.852)", 0), 0u) << json.substr(0, 40);
  EXPECT_NE(json.find(R"({"name":"32o","equity":0.32)"), std::string::npos);
  EXPECT_EQ(json.back(), ']');
  size_t classes = 0;
  for (size_t at = json.find("\"name\""); at != std::string::npos;
       at = json.find("\"name\"", at + 1)) {
    ++classes;
  }
  EXPECT_EQ(classes, 169u);
}

TEST(CalculatorRanges, TopRangesAreRangeJson) {
  // AA then KK: 12 hands, 0.9%, the closest to 1% (QQ+ would be 18, 1.4%).
  EXPECT_EQ(poker::rangeJson(poker::topRange(1)),
            R"({"ok":true,"text":"KK+","hands":[[45,44],[46,44],[46,45],[47,44],[47,45],[47,46],)"
            R"([49,48],[50,48],[50,49],[51,48],[51,49],[51,50]]})");
}

TEST(CalculatorCards, CardsJsonKeepsTheOrderAndNormalisesTheText) {
  // Js = 9 * 4 + 3, Ts = 8 * 4 + 3, 2h = 0 * 4 + 2.
  EXPECT_EQ(poker::cardsJson("jsTS2h"), R"({"ok":true,"text":"Js Ts 2h","cards":[39,35,2]})");
  EXPECT_EQ(poker::cardsJson("  "), R"({"ok":true,"text":"","cards":[]})");
  EXPECT_EQ(poker::cardsJson("As As"), R"({"ok":false,"message":"invalid cards: As As"})");
  EXPECT_EQ(poker::cardsJson("Js \"x"), R"({"ok":false,"message":"invalid cards: Js \"x"})");
}

TEST(Calculator, VersionComesFromTheBuild) {
  const std::string_view version = poker::version();
  EXPECT_FALSE(version.empty());
  EXPECT_NE(version, "unknown");
  EXPECT_EQ(std::count(version.begin(), version.end(), '.'), 2) << version;  // major.minor.patch
}

}  // namespace
