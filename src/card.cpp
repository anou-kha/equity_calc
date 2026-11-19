#include "poker/card.hpp"

#include <cctype>

namespace poker {
namespace {

constexpr std::string_view kRankChars = "23456789TJQKA";
constexpr std::string_view kSuitChars = "cdhs";

int rankFromChar(char ch) {
  const auto pos = kRankChars.find(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
  return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

int suitFromChar(char ch) {
  const auto pos = kSuitChars.find(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return pos == std::string_view::npos ? -1 : static_cast<int>(pos);
}

}  // namespace

std::optional<Card> Card::parse(std::string_view text) {
  if (text.size() != 2) return std::nullopt;
  const int rank = rankFromChar(text[0]);
  const int suit = suitFromChar(text[1]);
  if (rank < 0 || suit < 0) return std::nullopt;
  return Card(rank, suit);
}

std::string Card::toString() const {
  return {kRankChars[static_cast<size_t>(rank())], kSuitChars[static_cast<size_t>(suit())]};
}

std::optional<std::vector<Card>> parseCards(std::string_view text) {
  std::vector<Card> cards;
  CardMask seen = 0;
  size_t i = 0;
  while (i < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
      continue;
    }
    if (i + 1 >= text.size()) return std::nullopt;
    auto card = Card::parse(text.substr(i, 2));
    if (!card || (seen & card->mask())) return std::nullopt;
    seen |= card->mask();
    cards.push_back(*card);
    i += 2;
  }
  return cards;
}

CardMask toMask(const std::vector<Card>& cards) {
  CardMask m = 0;
  for (Card c : cards) m |= c.mask();
  return m;
}

}  // namespace poker
