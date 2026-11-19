I'm building a high-performance Texas Hold'em equity calculator in C++20 as a portfolio project
for software engineering internship applications. Read CLAUDE.md and README.md first: they
describe the goals, conventions, card encoding and roadmap. The repo already has a working CMake
build, GoogleTest tests, a CI workflow, and roadmap step 1 (card representation and parsing in
include/poker/card.hpp and src/card.cpp).

Your task this session is **roadmap step 2 (naive hand evaluator) and step 3 (exact equity via
enumeration)**. Please work in this order and stop for my review after each numbered stage:

### Stage A: plan
Before writing code, briefly propose:
- the public API for the evaluator and for equity (header names, function signatures, result
  types). I'm thinking roughly:
  - `HandRank evaluate7(CardMask cards)` where a higher `HandRank` beats a lower one, plus a
    `HandCategory category(HandRank)` (high card, pair, ..., straight flush) and a human-readable
    description;
  - `EquityResult exactEquity(const std::vector<HoleCards>& players, CardMask board, CardMask dead = 0)`
    returning per-player win %, tie %, and equity (wins + split share of ties), and the number of
    boards enumerated.
- how the naive evaluator will work (e.g. count ranks/suits from the mask, detect flush,
  straight including the A-2-3-4-5 wheel, then pairs/trips/quads, and pack the category plus
  kickers into a single comparable integer).
- the test plan.
Wait for me to approve or adjust the plan.

### Stage B: naive evaluator
- Implement `include/poker/evaluator.hpp` and `src/evaluator.cpp`, and add them to CMakeLists.txt.
- Keep it simple and obviously correct: this is the **reference implementation** that the
  optimised evaluator will be tested against later, so don't optimise it.
- Tests in `tests/test_evaluator.cpp`, covering at least:
  - every category is detected correctly, including edge cases: wheel straight (A2345), steel
    wheel straight flush, royal flush, a flush with 6–7 suited cards, two pair where there are
    three pairs among the 7 cards, full house from two sets of trips, and quads with the best
    kicker chosen;
  - ordering within a category: kickers decide (AK high vs AQ high), a higher two pair beats
    a lower one, and a wheel loses to a 6-high straight;
  - exact ties (board plays) compare equal;
  - a sanity check that enumerates **all C(52,5) = 2,598,960 five-card hands** and checks the
    category counts match the known frequencies (straight flush 36, four of a kind 624,
    full house 3,744, flush 5,108, straight 10,200, three of a kind 54,912, two pair 123,552,
    one pair 1,098,240, high card 1,302,540; royal flushes are counted within straight flushes).
    Also check there are exactly **7,462 distinct 5-card hand ranks**.
- Add an evaluator benchmark to `bench/` (hands/sec over a fixed seeded set of random 7-card
  hands) so we have a baseline number.

### Stage C: exact equity
- Implement `include/poker/equity.hpp` and `src/equity.cpp`.
- Enumerate every possible completion of the board (0, 3, 4 or 5 known board cards) with known
  hole cards for 2–9 players, excluding dead cards. Handle split pots correctly (a 3-way tie
  gives each player 1/3).
- Validate inputs (duplicate cards, wrong card counts) and return an error rather than crashing.
- Tests in `tests/test_equity.cpp` against known reference values (allowing a small tolerance
  only where rounding matters):
  - AsAh vs KsKh preflop ≈ 82% / 18%;
  - AsKs vs QhQd preflop ≈ 46% / 54%;
  - a river spot where the result is exactly 100/0 or an exact chop;
  - a flop spot you calculate by hand or verify with a second method;
  - a 3-way preflop spot.
  Use 2–3 reputable reference values and say where they come from in comments.
- Wire it into the CLI: `poker_cli AsKs QhQd --board Js Ts 2h` should print each player's win,
  tie and equity percentages plus the number of boards enumerated and the time taken.
- Add an equity benchmark (preflop heads-up exact enumeration time).

### Stage D: wrap up
- Run the full build with `-DPOKER_SANITIZE=ON` in Debug as well as a Release build. Every test
  must pass with no warnings, and the code must be clang-format clean.
- Update README.md (usage example, roadmap checkboxes, baseline benchmark numbers with the
  machine and compiler) and CLAUDE.md (mark roadmap steps done).
- Write `docs/design.md`: a short explanation of the card encoding, how the naive evaluator
  ranks hands, and how enumeration works, written so I could use it to prepare for interviews.
- Suggest a set of small commits with messages. Don't push anything.

Throughout: explain the key ideas as you go (I want to understand and be able to defend every
design decision in an interview), keep functions small and readable, and ask me if a
requirement is ambiguous rather than guessing.
