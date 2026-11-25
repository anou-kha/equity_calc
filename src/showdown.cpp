#include "showdown.hpp"

#include <algorithm>
#include <bit>
#include <utility>

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

std::vector<Range> singleHandRanges(std::span<const HoleCards> players) {
  std::vector<Range> ranges(players.size());
  for (size_t i = 0; i < players.size(); ++i) ranges[i].add(players[i]);
  return ranges;
}

size_t missingBoardCards(CardMask board) {
  return static_cast<size_t>(kBoardSize - std::popcount(board));
}

int cardsLeftAfterDeal(CardMask board, CardMask dead, size_t numPlayers) {
  return Card::kNumCards - std::popcount(board | dead) - 2 * static_cast<int>(numPlayers);
}

std::variant<LiveHands, EquityError> liveHands(std::span<const Range> players, CardMask board,
                                               CardMask dead) {
  if (players.size() < kMinPlayers) return EquityError::kTooFewPlayers;
  if (players.size() > kMaxPlayers) return EquityError::kTooManyPlayers;
  if ((board | dead) & ~kFullDeck) return EquityError::kInvalidCard;
  if (!isValidBoardSize(std::popcount(board))) return EquityError::kBadBoardSize;
  if (board & dead) return EquityError::kDuplicateCard;
  if (cardsLeftAfterDeal(board, dead, players.size()) < kBoardSize - std::popcount(board)) {
    return EquityError::kNotEnoughCards;
  }

  LiveHands hands;
  hands.reserve(players.size());
  for (const Range& range : players) {
    std::vector<CardMask> live;
    for (const HoleCards& hand : range.combos()) {
      const CardMask mask = hand[0].mask() | hand[1].mask();
      if (!(mask & (board | dead))) live.push_back(mask);
    }
    // A one-hand range is a known hand, and a known hand that uses a board or dead card is the
    // same mistake as in the HoleCards version, so it gets the same error.
    if (live.empty()) {
      return range.size() == 1 ? EquityError::kDuplicateCard : EquityError::kEmptyRange;
    }
    hands.push_back(std::move(live));
  }
  return hands;
}

EquityError noValidDealError(std::span<const Range> players) {
  const bool allKnown = std::all_of(players.begin(), players.end(),
                                    [](const Range& range) { return range.size() == 1; });
  return allKnown ? EquityError::kDuplicateCard : EquityError::kNoValidDeal;
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
