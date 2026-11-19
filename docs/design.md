# Design notes

How the equity calculator works, and why it is built this way. The pipeline so far:

```
"AsKs" "QhQd" --board "Js Ts 2h"
   │  parseHoleCards / parseCards          (card.hpp)
   ▼
CardMask bitsets (one bit per card)
   │  exactEquity: enumerate every board   (equity.hpp)
   ▼
evaluate(7 cards) -> HandRank per player   (evaluator.hpp)
   │  compare ranks, count wins / ties
   ▼
win %, tie %, equity % per player
```

## 1. Card encoding

Each card is an index in `[0, 52)`:

```
index = rank * 4 + suit      rank: 0='2' ... 12='A'      suit: 0=c 1=d 2=h 3=s
```

A set of cards is a `CardMask` (`uint64_t`) with bit `i` set when card `i` is in the set.

**Why a bitset?** Every set operation becomes a single CPU instruction, with no allocation and no
loops:

| Question | Code |
|---|---|
| Combine hole cards and board | `hole \| board` |
| Is this card already used? | `used & card` |
| Which cards are left in the deck? | `kFullDeck & ~used` |
| How many cards? | `std::popcount(mask)` |

**Why rank-major (`rank * 4 + suit`)?** The four cards of one rank sit in four adjacent bits, so
`(mask >> 4 * rank) & 0xF` gives which suits are present for that rank, and its popcount is how many
cards of that rank you hold. That is exactly what the evaluator needs to find pairs, trips and quads.

*Trade-off:* a suit-major layout (`suit * 13 + rank`) would instead make each suit a contiguous
13-bit block, which makes flush detection a single shift. Fast evaluators often prefer that; the
naive evaluator is happier with rank-major, and either works.

## 2. The naive hand evaluator

`HandRank evaluate(CardMask cards)` takes 5, 6 or 7 cards and returns the strength of the best
five-card hand. It is deliberately simple: it is the **reference implementation** that the fast
evaluator (roadmap step 5) will be tested against.

### Step 1: summarise the cards as rank sets

A *rank set* is a 13-bit mask: bit `r` set means rank `r` is present. `summarize()` builds:

- `ranks`, `pairs`, `trips`, `quads`: the ranks held at least 1, 2, 3 and 4 times;
- `suitRanks[4]`: the ranks held in each suit.

For `Ah Ad Kc Kd Qc Qd 2h`: `pairs = {A, K, Q}`, `ranks = {A, K, Q, 2}`, and the clubs hold `{K, Q}`.

### Step 2: test categories from strongest to weakest

The first category that matches is the answer:

1. **Straight flush:** a suit with ≥ 5 cards whose ranks contain a straight. The straight is looked for
   *inside that suit's ranks*, not among all ranks, so a 9-high straight plus an unrelated flush
   isn't a straight flush.
2. **Four of a kind:** best quad rank + highest other rank as the kicker.
3. **Full house:** best trips + best *other* rank held at least twice. This also handles two sets of
   trips (KKK 777 → Kings full of Sevens).
4. **Flush:** the five highest ranks of the flush suit (works with 6 or 7 suited cards).
5. **Straight:** see below.
6. **Three of a kind:** trips + two kickers.
7. **Two pair:** the top two pairs + the highest remaining rank. With three pairs (AA KK QQ 2), the
   third pair's rank can be the kicker (Queen).
8. **Pair:** pair + three kickers.
9. **High card:** the five highest ranks.

With at most 7 cards, only one suit can hold 5 or more cards. A flush also can't coexist with a full
house or quads: a flush uses 5 cards of one suit, leaving only 2 others, and trips plus a pair (or
quads) would need at least 3 cards of other suits.

### Straights and the wheel

A five-rank window slides from ace-high (`A K Q J T`) down to six-high (`6 5 4 3 2`), checking
`(ranks & window) == window`. The **wheel** `A-2-3-4-5` is the one straight where the ace plays low,
so it is checked last, separately, and its high card is the **5**. That makes it lose to a six-high
straight automatically.

### Step 3: pack the result into one comparable integer

```
bit:  23..20     19..16   15..12   11..8    7..4     3..0
      category   rank 1   rank 2   rank 3   rank 4   rank 5    (most important first)
```

Ranks fit in 4 bits (0..12); unused slots are 0. Because the most important information is in the
highest bits, **comparing two `HandRank`s as integers is the same as comparing the poker hands**,
just like comparing words letter by letter.

| Hand | Category | Ranks | Value |
|---|---|---|---|
| Two pair, Aces and Kings (Queen kicker) | 2 | A K Q | `0x2CBA00` |
| Straight, Five high (the wheel) | 4 | 5 | `0x430000` |
| Straight, Six high | 4 | 6 | `0x440000` |
| Flush, Ace-Jack-Nine-Six-Four | 5 | A J 9 6 4 | `0x5C9742` |

`HandRank` is a small class wrapping the `uint32_t`, not a bare integer. The compiler then rejects
accidental mixing with card indices or counts, and `auto operator<=>(const HandRank&) const = default;`
generates all six comparisons. It compiles to plain integer comparisons, so there's no runtime cost.

**Why exactly 7,462 distinct values for 5-card hands?** Each distinct hand strength gets exactly one
value, and there are 7,462 of them:

| Category | Distinct ranks | Why |
|---|---:|---|
| Straight flush | 10 | high card 5..A |
| Four of a kind | 156 | 13 quad ranks × 12 kickers |
| Full house | 156 | 13 × 12 |
| Flush | 1,277 | C(13,5) = 1,287 rank sets, minus the 10 straights |
| Straight | 10 | |
| Three of a kind | 858 | 13 × C(12,2) |
| Two pair | 858 | C(13,2) × 11 |
| Pair | 2,860 | 13 × C(12,3) |
| High card | 1,277 | same as flush |

Over all 7-card hands only **4,824** of them occur as the best five. Many weak hands (for example
7-5-4-3-2 high) can't be the best five out of seven, because the two extra cards always improve
them.

### How we know it's correct

- **Exhaustive 5-card test:** all C(52,5) = 2,598,960 hands produce the known category frequencies
  and exactly 7,462 distinct ranks.
- **Exhaustive 7-card test** (opt-in, ~10 s in Release): all 133,784,560 hands match the known
  frequencies and exactly 4,824 distinct ranks.
- **Second method:** for 100,000 random 7-card hands, `evaluate(7 cards)` equals the best of the
  C(7,2) = 21 five-card subsets. The 5-card logic is already proven exhaustively, so this checks the
  7-card logic (choosing the best five and the right kickers) with an independent method.
- **Hand-picked edge cases:** wheel, steel wheel, royal flush, 6–7 suited cards, three pairs, two sets
  of trips, quads with the best kicker, ordering within categories, and boards that play (exact ties).

Note that standard tables list royal flushes (4 five-card hands) separately from other straight
flushes (36). `evaluate()` puts royals in the straight flush category, so that category has 40.

### Performance

About 8.8 M seven-card hands per second (single thread, Release). Each call loops over 13 ranks and
4 suits and runs several branchy category checks. Lookup-table evaluators (roadmap step 5) replace
most of that with a few table reads, and will be checked against this one.

## 3. Exact equity by enumeration

`exactEquity(players, board, dead)` deals **every** possible completion of the board and scores each
showdown. Every completion is equally likely, so the counts are exact probabilities, with no
sampling error.

### What gets enumerated

The remaining deck is every card not in a hand, on the board, or dead. The missing board cards are
chosen from it in every possible way:

| Situation | Remaining | Missing | Boards |
|---|---:|---:|---:|
| Heads-up preflop | 48 | 5 | C(48,5) = 1,712,304 |
| 3-way preflop | 46 | 5 | C(46,5) = 1,370,754 |
| 9-way preflop | 34 | 5 | C(34,5) = 278,256 |
| Heads-up flop | 45 | 2 | C(45,2) = 990 |
| Heads-up turn | 44 | 1 | 44 |
| River | – | 0 | 1 |

### Visiting each combination exactly once

```cpp
template <typename Visit>
void forEachCombination(const Deck& deck, size_t count, size_t first, CardMask dealt,
                        const Visit& visit) {
  if (count == 0) { visit(dealt); return; }
  for (size_t i = first; i + count <= deck.size; ++i)
    forEachCombination(deck, count - 1, i + 1, dealt | deck.cards[i], visit);
}
```

After choosing card `i`, the recursion only considers cards *after* `i`. That is what makes each
*set* of cards appear once, instead of once per ordering (5! = 120 times for a full board).

`visit` is a **template parameter**, not a `std::function`. The compiler knows the exact lambda type
and can inline it: no heap allocation, no indirect call per board. The deck is a fixed-size
`std::array`, so the hot loop does no allocation at all (a project rule for hot paths).

### Scoring a board, and split pots

For each board, evaluate every player's 7 cards, find the best `HandRank`, and count how many players
share it (`k`):

- `k = 1`: that player gets a **win**;
- `k > 1`: each of the `k` players gets a **tie** and `1/k` of the pot.

**Win %** and **tie %** are fractions of all boards. **Equity** is the expected share of the pot:
wins plus split shares. Heads-up, that's `win + tie / 2`.

**Exact arithmetic with "pot units":** instead of adding `1.0 / k` as a `double`, each pot is worth
**2520 units**. 2520 = lcm(1, 2, ..., 9) is divisible by every possible number of tied players
(at most 9), so a k-way split gives each player a whole number `2520 / k` (a 3-way chop is 840
each). All totals stay exact integers, and equity only becomes a `double` in a single division at the
end. That buys two things:

- exact tests: a chop is *exactly* `0.5`, and a 3-way chop is exactly `1.0 / 3.0`;
- a strong invariant: the units handed out must equal exactly `boards × 2520`.

Adding up 1.7 million doubles would accumulate rounding error, and 1/3 can't be represented
exactly anyway.

### Input validation

Validation happens once, before enumeration, in this order: player count (2–9), card indices in
range, board size (0/3/4/5), duplicate cards anywhere (hands, board, dead), and enough cards left
to complete the board.

Errors are returned, not thrown: `std::variant<EquityResult, EquityError>`. Invalid input is an
expected condition at an API boundary (a typo in the CLI), not an exceptional one, and the project
avoids exceptions in the core library. `EquityError` is an enum, so the CLI can print a specific
message. C++23's `std::expected` is the purpose-built version of this pattern; the project targets
C++20.

Making `HoleCards` a `std::array<Card, 2>` means a player with 1 or 3 hole cards can't even be
constructed, so only the board size needs a runtime check.

### Performance

Heads-up preflop takes about 230 ms (Release, single thread): 1.7M boards × 2 evaluations. Almost all
of that is in `evaluate()`, so the fast evaluator will speed up equity roughly in proportion.
Multithreading (step 6) can split the combinations between threads. Monte Carlo (step 4) is for
cases where enumeration is too slow, such as ranges.

Interestingly, enumeration evaluates about 15 M hands/s, faster than the 8.8 M/s random-hand
benchmark. The likely reason is that consecutive boards in enumeration order are similar, so the
CPU's branch predictor guesses the evaluator's branches well, whereas random hands defeat it.

## 4. Testing approach

- **Reference values**, checked to the published two decimals:
  - AsAh vs KsKh: 82.36 / 17.09 / 0.54 tie
  - AhAs vs KdKc: 81.06 / 18.55 / 0.38
  - AsKs vs QhQd: 46.02 / 53.59 / 0.39
  
  Sources are cited in `tests/test_equity.cpp`. **Suits matter:** AA vs KK equity is 81.26%, 81.95%
  or 82.64% depending on whether the kings share 0, 1 or 2 of the aces' suits. Shared suits mean the
  kings' flushes are often beaten by a bigger flush. The commonly quoted "≈ 81.9%" matches the
  one-shared-suit case, which is also very close to the average over all suit combinations.
- **A 3-way preflop spot** (KK vs QQ vs 22) against a published, loosely rounded figure.
- **Spots worked out by hand:** KhKd vs QcQs on Kc 7s 2d. The queens win only with exactly Qh + Qd on
  turn and river, so 1 board in 990.
- **Exact results:** a river win (100/0), a board that plays (exactly 50/50), a 3-way chop (exactly
  1/3 each), and two players chopping while a third loses.
- **Invariants:** the pot is always fully distributed; swapping players swaps their results;
  renaming suits consistently changes nothing.
- **Validation:** every `EquityError`, including the boundary (exactly five cards left is fine, four
  is an error).

### Reproducible randomness

Tests and benchmarks use `std::mt19937_64` with a fixed seed and its *raw* output (`rng() % 52`),
not `std::uniform_int_distribution`. The C++ standard specifies `mt19937_64` bit for bit, but leaves
the distributions' algorithms up to each standard library, so the same seed gives different numbers
on MSVC and GCC. With raw output, the benchmark's "1M random hands" are identical everywhere: MSVC
and GCC builds print the same checksum. This will matter again for seeded Monte Carlo tests.

## 5. Build and tooling notes

- **Sanitizers:** `add_compile_options()` only affects targets created *after* it. The original
  `CMakeLists.txt` added the sanitizer flags after `add_library(poker)`, so the library itself was
  never instrumented. The block now comes first.
- **MSVC has AddressSanitizer but no UBSan**, and ASan is incompatible with MSVC's default Debug
  runtime checks (`/RTC1`) and incremental linking, so `POKER_SANITIZE` handles MSVC separately and
  copies the ASan runtime DLL next to each executable.
- Warnings: the library builds cleanly with `/W4` (MSVC) and with
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wsign-conversion` (GCC 15).

## 6. Quick answers to likely interview questions

- **Why a bitmask instead of `std::vector<Card>`?** Set operations become single instructions, there
  is no allocation, and a whole hand fits in one register.
- **How do you compare two hands?** Pack category + deciding ranks into one integer, most important
  bits first; integer comparison is then poker comparison.
- **Why is the wheel special?** It's the only straight where the ace plays low, so its high card is
  the 5, and it loses to a six-high straight.
- **How do you know the evaluator is right?** It is checked against all 2.6M five-card and all 133.8M
  seven-card hands' known frequencies and distinct-rank counts, plus an independent best-of-21-subsets
  method.
- **Why 2520?** It's the least common multiple of 1..9, so every split pot divides into whole units
  and results stay exact.
- **What's the complexity?** C(remaining, missing) boards × players evaluations. That is 1.7M × 2
  heads-up preflop, and it explodes for ranges, which is why Monte Carlo comes next.
- **Why `std::variant` and not exceptions?** Invalid input is expected at an API boundary, the core
  library avoids exceptions, and the error enum lets callers react to the specific problem.
- **What would you do next for speed?** A lookup-table evaluator (step 5), then multithreading
  (step 6), always testing against this reference implementation.
