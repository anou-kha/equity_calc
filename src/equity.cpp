// Exact equity by enumeration.
//
// Idea: every card that isn't in a player's hand, on the board, or dead could still be dealt. We
// visit every combination of the missing board cards from that remaining deck exactly once, score
// the resulting showdown, and count wins and ties per player. Because every board is equally
// likely, the counts divided by the number of boards are exact probabilities: no sampling error.
//
// Trade-off: the number of boards grows fast (1.7M heads-up preflop), so this is only practical for
// known hole cards; Monte Carlo (roadmap step 4) handles the cases where enumeration is too slow.
// The inner loop does no heap allocation: the deck and per-player data live in fixed-size arrays.
#include "poker/equity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <optional>

#include "poker/evaluator.hpp"

namespace poker {
namespace {

constexpr int kBoardSize = 5;
constexpr CardMask kFullDeck = (CardMask{1} << Card::kNumCards) - 1;

bool isValidBoardSize(int size) { return size == 0 || (size >= 3 && size <= kBoardSize); }

std::optional<EquityError> validate(std::span<const HoleCards> players, CardMask board,
                                    CardMask dead) {
  if (players.size() < kMinPlayers) return EquityError::kTooFewPlayers;
  if (players.size() > kMaxPlayers) return EquityError::kTooManyPlayers;
  if ((board | dead) & ~kFullDeck) return EquityError::kInvalidCard;
  if (!isValidBoardSize(std::popcount(board))) return EquityError::kBadBoardSize;
  if (board & dead) return EquityError::kDuplicateCard;

  CardMask used = board | dead;
  for (const HoleCards& hole : players) {
    for (const Card card : hole) {
      if (card.index() >= Card::kNumCards) return EquityError::kInvalidCard;
      if (used & card.mask()) return EquityError::kDuplicateCard;
      used |= card.mask();
    }
  }
  const int cardsLeft = std::popcount(kFullDeck & ~used);
  if (cardsLeft < kBoardSize - std::popcount(board)) return EquityError::kNotEnoughCards;
  return std::nullopt;
}

// The cards that can still be dealt, each as a single-bit mask, in a fixed-size array.
struct Deck {
  std::array<CardMask, Card::kNumCards> cards{};
  size_t size = 0;
};

Deck remainingDeck(CardMask used) {
  Deck deck;
  for (int index = 0; index < Card::kNumCards; ++index) {
    const CardMask card = CardMask{1} << index;
    if (!(used & card)) deck.cards[deck.size++] = card;
  }
  return deck;
}

// Calls visit(dealt | chosen) for every way of choosing `count` cards from deck.cards[first..],
// each combination exactly once. Choosing card i and then only cards after it is what stops the
// same set being visited in a different order.
//
// `visit` is a template parameter rather than a std::function, so the compiler can inline it and
// there is no heap allocation or indirect call per board.
template <typename Visit>
void forEachCombination(const Deck& deck, size_t count, size_t first, CardMask dealt,
                        const Visit& visit) {
  if (count == 0) {
    visit(dealt);
    return;
  }
  for (size_t i = first; i + count <= deck.size; ++i) {
    forEachCombination(deck, count - 1, i + 1, dealt | deck.cards[i], visit);
  }
}

// Scores one complete board: the single best hand wins; if several players share the best hand,
// each of them gets an equal share of the pot.
void scoreBoard(CardMask board, std::span<const CardMask> holes, std::span<PlayerResult> results) {
  std::array<HandRank, kMaxPlayers> ranks;
  HandRank best;
  for (size_t i = 0; i < holes.size(); ++i) {
    ranks[i] = evaluate(board | holes[i]);
    best = std::max(best, ranks[i]);
  }
  uint64_t numBest = 0;
  for (size_t i = 0; i < holes.size(); ++i) {
    if (ranks[i] == best) ++numBest;
  }
  for (size_t i = 0; i < holes.size(); ++i) {
    if (ranks[i] != best) continue;
    if (numBest == 1) {
      ++results[i].wins;
    } else {
      ++results[i].ties;
      results[i].tieShares += kPotUnits / numBest;
    }
  }
}

double fraction(uint64_t part, uint64_t whole) {
  return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
}

}  // namespace

double EquityResult::win(size_t player) const { return fraction(players[player].wins, boards); }

double EquityResult::tie(size_t player) const { return fraction(players[player].ties, boards); }

double EquityResult::equity(size_t player) const {
  const PlayerResult& p = players[player];
  return fraction(p.wins * kPotUnits + p.tieShares, boards * kPotUnits);
}

std::string_view toString(EquityError error) {
  switch (error) {
    case EquityError::kTooFewPlayers:
      return "at least 2 players are needed";
    case EquityError::kTooManyPlayers:
      return "at most 9 players are supported";
    case EquityError::kInvalidCard:
      return "invalid card";
    case EquityError::kBadBoardSize:
      return "the board must have 0, 3, 4 or 5 cards";
    case EquityError::kDuplicateCard:
      return "the same card appears more than once";
    case EquityError::kNotEnoughCards:
      return "not enough cards left to complete the board";
  }
  return "unknown error";
}

std::variant<EquityResult, EquityError> exactEquity(std::span<const HoleCards> players,
                                                    CardMask board, CardMask dead) {
  if (const auto error = validate(players, board, dead)) return *error;

  std::array<CardMask, kMaxPlayers> holeMasks{};
  CardMask used = board | dead;
  for (size_t i = 0; i < players.size(); ++i) {
    holeMasks[i] = players[i][0].mask() | players[i][1].mask();
    used |= holeMasks[i];
  }
  const std::span<const CardMask> holes(holeMasks.data(), players.size());

  EquityResult result;
  result.players.resize(players.size());
  const auto missing = static_cast<size_t>(kBoardSize - std::popcount(board));
  forEachCombination(remainingDeck(used), missing, 0, board, [&](CardMask fullBoard) {
    scoreBoard(fullBoard, holes, result.players);
    ++result.boards;
  });
  return result;
}

}  // namespace poker
