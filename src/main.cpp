// poker_cli: command-line front end for the equity engine.
//
// Planned usage (to be implemented):
//   poker_cli AsKs QhQd --board JsTs2h
//   poker_cli "AKs,QQ+" "random" --iterations 1000000
//
// For now it just parses and echoes cards so the build is wired end to end.
#include <iostream>

#include "poker/card.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: poker_cli <cards> [<cards> ...]\n";
    return 1;
  }
  for (int i = 1; i < argc; ++i) {
    auto cards = poker::parseCards(argv[i]);
    if (!cards) {
      std::cerr << "invalid cards: " << argv[i] << '\n';
      return 1;
    }
    for (const auto& c : *cards) std::cout << c.toString() << ' ';
    std::cout << '\n';
  }
  return 0;
}
