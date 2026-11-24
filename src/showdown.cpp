#include "showdown.hpp"

#include <algorithm>
#include <bit>
#include <cassert>

#include "poker/evaluator.hpp"

namespace poker::detail {
namespace {

constexpr CardMask kFullDeck = (CardMask{1} << Card::kNumCards) - 1;

bool isValidBoardSize(int size) { return size == 0 || (size >= 3 && size <= kBoardSize); }

}  // namespace

std::optional<EquityError> validateDeal(std::span<const HoleCards> players, CardMask board,
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

size_t missingBoardCards(CardMask board) {
  return static_cast<size_t>(kBoardSize - std::popcount(board));
}

Seats seatPlayers(std::span<const HoleCards> players) {
  assert(players.size() <= kMaxPlayers);
  Seats seats;
  for (const HoleCards& hole : players) {
    const CardMask mask = hole[0].mask() | hole[1].mask();
    seats.holes[seats.count++] = mask;
    seats.allHoleCards |= mask;
  }
  return seats;
}

Deck remainingDeck(CardMask used) {
  Deck deck;
  for (int index = 0; index < Card::kNumCards; ++index) {
    const CardMask card = CardMask{1} << index;
    if (!(used & card)) deck.cards[deck.size++] = card;
  }
  return deck;
}

PotShares showdown(CardMask board, std::span<const CardMask> holes) {
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
  PotShares shares{};
  for (size_t i = 0; i < holes.size(); ++i) {
    if (ranks[i] == best) shares[i] = kPotUnits / numBest;
  }
  return shares;
}

void addToCounts(const PotShares& shares, std::span<PlayerResult> counts) {
  for (size_t i = 0; i < counts.size(); ++i) {
    if (shares[i] == kPotUnits) {
      ++counts[i].wins;
    } else if (shares[i] > 0) {
      ++counts[i].ties;
      counts[i].tieShares += shares[i];
    }
  }
}

void addCounts(std::span<const PlayerResult> from, std::span<PlayerResult> into) {
  for (size_t i = 0; i < into.size(); ++i) {
    into[i].wins += from[i].wins;
    into[i].ties += from[i].ties;
    into[i].tieShares += from[i].tieShares;
  }
}

}  // namespace poker::detail
