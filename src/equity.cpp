// Exact equity by enumeration.
//
// Idea: every card that isn't in a player's hand, on the board, or dead could still be dealt. We
// visit every combination of the missing board cards from that remaining deck exactly once, score
// the resulting showdown, and count wins and ties per player. Because every board is equally
// likely, the counts divided by the number of boards are exact probabilities: no sampling error.
//
// Trade-off: the number of boards grows fast (1.7M heads-up preflop), so this is only practical for
// known hole cards; Monte Carlo (monte_carlo.hpp) handles the cases where enumeration is too slow.
// The inner loop does no heap allocation: the deck and per-player data live in fixed-size arrays.
#include "poker/equity.hpp"

#include "showdown.hpp"

namespace poker {
namespace {

// Calls visit(dealt | chosen) for every way of choosing `count` cards from deck.cards[first..],
// each combination exactly once. Choosing card i and then only cards after it is what stops the
// same set being visited in a different order.
//
// `visit` is a template parameter rather than a std::function, so the compiler can inline it and
// there is no heap allocation or indirect call per board.
template <typename Visit>
void forEachCombination(const detail::Deck& deck, size_t count, size_t first, CardMask dealt,
                        const Visit& visit) {
  if (count == 0) {
    visit(dealt);
    return;
  }
  for (size_t i = first; i + count <= deck.size; ++i) {
    forEachCombination(deck, count - 1, i + 1, dealt | deck.cards[i], visit);
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
    case EquityError::kNoTrials:
      return "Monte Carlo needs at least 1 trial";
  }
  return "unknown error";
}

std::variant<EquityResult, EquityError> exactEquity(std::span<const HoleCards> players,
                                                    CardMask board, CardMask dead) {
  if (const auto error = detail::validateDeal(players, board, dead)) return *error;

  const detail::Seats seats = detail::seatPlayers(players);
  const detail::Deck deck = detail::remainingDeck(board | dead | seats.allHoleCards);

  EquityResult result;
  result.players.resize(players.size());
  forEachCombination(deck, detail::missingBoardCards(board), 0, board, [&](CardMask fullBoard) {
    detail::addToCounts(detail::showdown(fullBoard, seats.holeMasks()), result.players);
    ++result.boards;
  });
  return result;
}

}  // namespace poker
