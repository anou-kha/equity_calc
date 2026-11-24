# Design notes

How the equity calculator works, and why it is built this way. The pipeline so far:

```
"QQ+, AKs" "random" --board "Js Ts 2h"
   │  parseRange / parseCards                       (range.hpp, card.hpp)
   ▼
Ranges of hands, CardMask bitsets (one bit per card)
   │  exactEquity: every deal of the hands x every board   (equity.hpp)
   │  monteCarloEquity: N random deals and boards          (monte_carlo.hpp)
   │  ... shared out between threads                       (src/parallel.hpp)
   ▼
evaluate(7 cards) -> HandRank per player            (evaluator.hpp, lookup tables)
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

`HandRank evaluateNaive(CardMask cards)` takes 5, 6 or 7 cards and returns the strength of the best
five-card hand. It is deliberately simple: it is the **reference implementation**. The fast
evaluator (section 3) is built from it and tested against it.

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
- **Second method:** for 100,000 random 7-card hands, `evaluateNaive(7 cards)` equals the best of the
  C(7,2) = 21 five-card subsets. The 5-card logic is already proven exhaustively, so this checks the
  7-card logic (choosing the best five and the right kickers) with an independent method.
- **Hand-picked edge cases:** wheel, steel wheel, royal flush, 6–7 suited cards, three pairs, two sets
  of trips, quads with the best kicker, ordering within categories, and boards that play (exact ties).

Note that standard tables list royal flushes (4 five-card hands) separately from other straight
flushes (36). The evaluators put royals in the straight flush category, so that category has 40.

### Performance

About 9 M seven-card hands per second (single thread, Release). Each call loops over 13 ranks and 4
suits and runs several branchy category checks. The fast evaluator (section 3) replaces almost all
of that with a few table lookups.

## 3. The fast evaluator

`HandRank evaluate(CardMask cards)` returns exactly the same values as `evaluateNaive()`, about 9×
faster: 80–90 M hands/s. Exact equity and Monte Carlo both use it.

### The idea: answer every question in advance

Instead of working out each hand from scratch, the fast evaluator computes the answer for every
possible situation once, stores it in a table, and then only has to work out *which* table entry a
hand belongs to. Two facts keep the tables small:

1. **Flushes depend only on the flush suit's ranks.** With 5–7 cards, a flush can't coexist with a
   full house or quads (section 2). So if a suit holds 5 or more cards, the answer depends only on
   which ranks that suit holds: a 13-bit rank set indexes a **flush table** of 2¹³ = 8,192 entries.
2. **Otherwise suits don't matter at all,** only how many cards of each rank there are: 13 numbers
   from 0 to 4 that add up to the number of cards. For 7 cards there are only 49,205 such count
   vectors.

### Counting the cards of each rank in two lines

The card encoding puts each rank's four cards in four adjacent bits (one nibble), so a rank's count
is the popcount of its nibble. The first two steps of the classic bit-counting trick do that for all
13 nibbles at once:

```cpp
const uint64_t pairCounts = cards - ((cards >> 1) & 0x5555555555555555);  // per 2 bits
return (pairCounts & 0x3333333333333333) + ((pairCounts >> 2) & 0x3333333333333333);  // per 4 bits
```

### The perfect hash: numbering the count vectors

A table needs an index, so each count vector needs a number. The trick is to imagine all the vectors
with the same total listed in order (lexicographically, rank 0's count first) and use a vector's
**position in that list**. With only 3 ranks and 2 cards, for example:

| Position | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| Counts (rank 0, 1, 2) | 0 0 2 | 0 1 1 | 0 2 0 | 1 0 1 | 1 1 0 | 2 0 0 |

To find the position of `1 1 0` without writing the list out, count the vectors that come before
it, rank by rank:

- Vectors with **fewer than 1** card on rank 0 come first. That's every way to put both cards on
  ranks 1–2: **3** vectors.
- Then, among vectors with exactly 1 card on rank 0, those with **fewer than 1** card on rank 1: the
  remaining card must go on rank 2, so **1** vector.
- Rank 2 has 0 cards, so nothing comes before it there. The position is 3 + 1 = **4**.

Each step only needs "how many ways can the remaining ranks hold the remaining cards?", which
depends on just three small numbers (the rank, the cards left, and this rank's count). So it is
precomputed into a 13 × 8 × 5 table of offsets, and hashing a hand is **one table lookup and one
addition per rank**.

The hash is **perfect** (no two vectors share a number) and **minimal** (no gaps), so the tables
have exactly one entry per possibility: 6,175 for 5 cards, 18,395 for 6 and 49,205 for 7. Those are
the counts of ways to choose k ranks with repetition, C(12 + k, k), minus those with 5 or more of
one rank, and a test checks the table sizes match them.

### Building the tables from the reference

Every table entry is filled in by asking `evaluateNaive()` about one representative hand:

- **Flush table:** for each set of 5–7 ranks, an all-clubs hand with those ranks.
- **Count tables:** for each count vector, a hand with exactly those counts, with suits dealt
  round-robin (clubs, diamonds, hearts, spades, clubs, ...). No suit then gets more than 2 of 7
  cards, so there's never a flush, and the cards of one rank always get different suits.

That's 78,494 naive evaluations (4,719 flush rank sets plus 73,775 count vectors), roughly 6 ms,
done once. The tables take about 330 KB, small
enough to stay in the CPU's L2 cache. Because they're built from the reference, the fast evaluator
gives the same answers by construction; the tests then confirm it on every hand.

The tables live in a function-local `static`, which C++ guarantees to initialise exactly once even if
several threads call `evaluate()` for the first time simultaneously ("magic statics"). That matters
for multithreading (section 6). `buildEvaluatorTables()` builds them up front, so timings (and later a
user interface) don't include the one-off cost.

`evaluate()` itself is then short:

```cpp
for (size_t suit = 0; suit < kNumSuits; ++suit) {
  const size_t count = suitCount(cards, suit);
  if (count >= 5) return t.flush[suitRanks(cards, suit)];  // about 3% of 7-card hands
  numCards += count;
}
return t.nonFlush[numCards][nonFlushIndex(t.offsets, cards, numCards)];
```

### Two measured optimisations

The first working version ran at about 48 M hands/s (MSVC), only 5× the naive evaluator. Measuring
found two problems:

1. **An early exit made the loop slower.** The hash loop originally stopped as soon as every card was
   counted. That sounds cheaper, but it stops at a different rank for every hand, so the CPU's branch
   predictor kept guessing wrong, and each wrong guess costs about 15–20 cycles. Always running all
   13 steps (the extra ones add 0) took MSVC from **48 to 79 M hands/s**, and a fixed-length loop can
   also be fully unrolled by the compiler.
2. **`std::popcount` was a function call on GCC.** Without CPU-specific build flags (`-mpopcnt`), GCC
   can't assume the CPU has a popcount instruction, so it calls a slower library routine. The suit
   counts now use a multiplication instead: a suit's cards, shifted down, are one bit per rank at
   bits 0, 4, 8, …, 48, and multiplying by `0x1111…1` adds every nibble into the ones above it, so the
   top nibble ends up holding the total (at most 13, so it never overflows). GCC went from **57 to
   93 M hands/s**; MSVC, which checks for the instruction at runtime, stayed about the same.

The lesson worth telling in an interview: measure, because the "obvious" optimisation can make code
slower, and compiler flags matter as much as the algorithm.

### How it's tested

- **Every behaviour test runs on both evaluators** (they are parameterised): categories, edge cases,
  ordering, ties, and the exhaustive 5-card frequencies.
- **Fast == naive on every hand:** all 2,598,960 five-card hands and 500,000 random 6/7-card hands on
  every test run, plus all 20,358,520 six-card and 133,784,560 seven-card hands as an opt-in test
  (about 11 s in Release). So the two are equal by exhaustive check, not by sampling.
- **The tables are minimal:** their sizes must be exactly 6,175 / 18,395 / 49,205.
- **Thread safety:** 8 threads race to make the first call, and every answer must still be right.

### Alternatives considered

- **The "Two Plus Two" evaluator:** a state machine where each card is one table lookup. It's even
  faster, but the table is about 130 MB, slow to build, and too big for a WebAssembly build.
- **Cactus Kev's 5-card evaluator:** small tables, but a 7-card hand needs all 21 five-card subsets,
  so it's barely faster than the naive evaluator.

Ideas for later, each needing a measurement first: hashing two ranks per lookup (half as many
steps), and in exact enumeration, reusing the board's rank counts across all the players' hands.

## 4. Exact equity by enumeration

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

Heads-up preflop takes about 70 ms with MSVC and 50 ms with GCC (Release, single thread): 1.7M
boards × 2 evaluations. With the naive evaluator it took about 230 ms. That 3–4× gain is smaller
than the evaluator's 8–10×, because the evaluator is no longer nearly all of the work: enumerating
boards and comparing ranks now take a visible share. Multithreading (section 6) shares the boards
between threads, about 7× faster on 8 cores. Monte Carlo (section 5) is for cases where enumeration
is too slow, such as ranges.

Interestingly, with the naive evaluator, enumeration evaluated about 15 M hands/s, faster than the
8.8 M/s random-hand benchmark. The likely reason is that consecutive boards in enumeration order are
similar, so the CPU's branch predictor guessed the evaluator's branches well, whereas random hands
defeat it.

## 5. Monte Carlo equity

`monteCarloEquity(players, board, dead, {.trials = N, .seed = S})` deals N random completions of the
board and averages the results. It uses the same validation, deck and showdown code as `exactEquity`
(shared through the internal `src/showdown.hpp`). The two methods therefore differ only in *which*
boards they score, which is exactly what the tests compare.

### Why the error shrinks like 1/√N

Each trial gives a player a pot share `X`: 1, `1/k` for a k-way split, or 0. The estimate is the
average of N independent samples of `X`, and its **standard error** is `σ / √N`, where `σ` is the
spread of a single trial. So four times the trials halve the error, and 100 times the trials give one
more correct decimal place. Heads-up, `σ` is about 0.4–0.5 (0.5 for a coin flip, less for a
lopsided spot like AA vs KK), so 1M trials give a standard error of about 0.04–0.05% and a 95% margin
of about ±0.08–0.1%.

### Dealing a random board

```cpp
for (size_t i = 0; i < count; ++i) {
  const auto j = i + static_cast<size_t>(uniformBelow(rng, deck.size - i));
  std::swap(deck.cards[i], deck.cards[j]);
  board |= deck.cards[i];
}
```

This is a *partial* Fisher–Yates shuffle. Position `i` swaps with a uniformly chosen position from
`i` onwards, so after `count` steps the first `count` cards are a uniformly random selection. That
costs one random number per missing card, with no allocation. The deck array is never reset
between trials, because shuffling an already-shuffled deck is still uniform.

### Unbiased random numbers: `uniformBelow`

`rng() % n` looks uniform but isn't quite. The 2⁶⁴ possible raw values form whole blocks of `n` plus
one incomplete block, and the incomplete block gives the smallest results an extra chance.
`uniformBelow` rejects raw values from that incomplete block and draws again. For `n = 52` a
rejection happens about once in 10¹⁸ draws, so it costs nothing in practice, but the result is
provably uniform. A test with `n = 3·2⁶²`, where plain modulo makes the lowest third of results
come up half the time, shows that the bias is real and that `uniformBelow` doesn't have it.

`std::uniform_int_distribution` is unbiased too, but the standard doesn't specify its algorithm, so
MSVC and GCC produce different numbers from the same seed. Our own function keeps results identical
on every platform.

### The margin of error

Per player, the code keeps two integers: the sum of pot units won (`S`) and the sum of their
squares (`Q`). At the end:

```
mean      = S / N
variance  = (Q / N - mean²) × N / (N - 1)      (N / (N - 1) corrects for measuring the spread
                                                around the sample's own mean: Bessel's correction)
std error = √(variance / N)
95% CI    = mean ± 1.96 × std error
```

If every trial is identical (a complete board), both terms are the same exactly-representable
number, so the margin is *exactly* 0.

*Limitations:* the 1.96 comes from the normal approximation, which is accurate for many trials but
not near 0% or 100%. A player who wins 0 of 1,000 trials gets "± 0", which overstates the certainty.
The Wilson score interval handles that case better and would be a natural refinement. The margins
are also per player; the players' estimates are linked, since they always add up to 100%.

### Reproducible results, now and with threads

The trials run in chunks of 65,536. Chunk `c` gets its own generator, seeded with
`mixSeed(seed, c)`, and starts from a fresh copy of the deck, so no chunk depends on any other.

- `mixSeed` is **SplitMix64**: it scrambles `(seed, c)` so that neighbouring chunks get unrelated
  generator states. Seeding chunk `c` with plain `seed + c` would start the streams from very similar
  states.
- All the totals are **integers**, and integer addition gives the same result in any order.
  Floating-point addition doesn't: `(a + b) + c` can differ from `a + (b + c)` in the last bits.

Together, these mean the result depends only on the inputs, the trial count and the seed.
Multithreading (section 6) relies on exactly this: chunks run on any number of threads, finishing
in any order, and the result stays bit-identical.

### How it's tested

Randomness and tests mix badly unless the randomness is controlled, so every test uses a fixed seed
and is fully deterministic. The statistics then decide *how strict* each check can be:

- **Agreement with exact:** on five spots, from heads-up preflop to a 9-way flop, every equity and
  win estimate must be within 4 standard errors of the exact answer. A correct implementation
  misses by that much only about once in 16,000 checks, so the test would pass with almost any seed.
- **Coverage:** 400 independent runs of 1,000 trials each. Their 95% intervals must contain the exact
  value 92–98% of the time. This tests the confidence-interval formula itself, not just one number.
- **Known answers:** the standard-error formula on a tiny hand-computed sample; a complete board gives
  a margin of exactly 0; dead cards are never dealt; trial counts are exact across chunk boundaries.
- **Mutation testing:** I deliberately broke the shuffle with an off-by-one, so that one card could
  never be dealt. Coverage fell to 79%, and 3 of the 5 agreement spots failed. AsKs vs QhQd preflop
  came out at 42.1% instead of 46.2%, because an ace could never come. The 4-way and 9-way spots still
  passed: the missing A♠ hurt every player there about equally. That is why the tests use several
  different spots.

### When to use which

| AsAh vs KsKh preflop (Release, MSVC) | Time | Answer |
|---|---|---|
| Exact: all 1,712,304 boards | ≈ 70 ms | 82.64%, exact |
| Monte Carlo: 1,000,000 boards | ≈ 75 ms | 82.61% ± 0.07% |

(With the naive evaluator these took about 230 ms and 240 ms.) For known hole cards, exact
enumeration wins: it gives the exact answer in about the same time.

Each Monte Carlo trial also costs more than an enumerated board: about 75 ns against 40 ns. Timing
trials that deal different numbers of cards shows where it goes. A trial costs about 36 ns when
nothing is dealt (a complete board), 47 ns with 1 card to deal, 68 ns with 2 and 85 ns with 5. So
dealing is likely more than half of a preflop trial, most probably because of the two 64-bit
divisions per card inside `uniformBelow`. A division-free method (Lemire's) is the obvious next
optimisation. It would change which boards a given seed deals, though, so it's a deliberate decision
rather than a quiet fix.

Monte Carlo pays off when the exact space explodes, which is what ranges (section 7) do. Exact
equity for AsAh against *one random hand* needs all C(50,2) = 1,225 opponent hands × 1,712,304
boards ≈ 2.1 billion showdowns: about 10 s on 16 threads, over a minute on one. Monte Carlo gets
within ±0.1% in a fraction of a second.

## 6. Multithreading

Exact enumeration and Monte Carlo can both share their work between threads
(`ExactOptions{.threads}`, `MonteCarloOptions{.threads}`, `--threads` in the CLI). The library
defaults to 1 thread, so it never starts threads behind a caller's back; a server or a web page may
manage threads itself. The CLI uses all hardware threads by default.

### Same answer on any number of threads

Two rules make the result independent of the number of threads:

1. **Split the work into tasks whose result depends only on the task's number,** never on which
   thread runs it or when. For Monte Carlo a task is a 65,536-trial chunk, and chunk `c` always uses
   random stream `mixSeed(seed, c)` and the same starting deck, exactly as section 5 set up. For
   exact enumeration a task is "every board that starts with these two missing cards".
2. **Keep the totals as integers and combine them at the end.** Integer addition gives the same
   answer in any order, so it doesn't matter which thread finishes first.

The tests check that 1, 2, 3, 5, 8 and 64 threads, and "all of them", give bit-identical results.

### How the threads share the work

One small helper, `detail::runInParallel` in `src/parallel.hpp`, does this for both methods. Each
thread, including the one that called it, runs:

```cpp
State local{};
for (uint64_t i = nextTask.fetch_add(1, std::memory_order_relaxed); i < numTasks;
     i = nextTask.fetch_add(1, std::memory_order_relaxed)) {
  task(i, local);
}
states[threadIndex] = local;
```

- **An atomic counter hands out tasks.** A thread that finishes early just takes the next task, so
  nobody waits for a slow thread's fixed share. Splitting the work up front (thread `t` gets tasks
  `t`, `t + T`, ...) would make everyone wait for the slowest thread.
- **Each thread adds into its own local totals** and copies them out once at the end. So there are no
  locks, and no **false sharing**: threads that update different variables sitting in the same 64-byte
  cache line still slow each other down, because the CPU's caches pass the whole line back and forth
  between cores.
- **`memory_order_relaxed` is enough** for the counter. It only has to give each task to exactly one
  thread, which any atomic increment guarantees. The results reach the calling thread through
  `join()`, which guarantees everything a thread wrote is visible afterwards.
- **The calling thread works too,** so `threads = 1` starts no threads at all. If the operating system
  refuses to start a thread, the ones already running share its tasks, and every task still runs
  exactly once.
- **No more threads than there is work for.** Monte Carlo uses at most one thread per chunk. Exact
  enumeration gives each thread at least 25,000 boards: starting a thread costs tens of
  microseconds, about as long as scoring a thousand boards, so a flop or a turn stays on one thread.
- **A new `std::thread` per call** rather than a persistent thread pool. Starting threads costs well
  under a millisecond, which is small next to the work. A pool would only pay off for many tiny
  calls, and it adds real complexity.

### Task size: a measured fix

The first version split exact enumeration by the first missing card only: 44 tasks. But they're
very uneven. Task 0 (every board starting with the deck's first card) has C(47,4) = 178,365 of the
1,712,304 boards, over 10% of the work. With 16 threads on 8 cores, each thread runs at about half a
core's speed, so that one task alone takes about 2 × 10% × 67 ms ≈ 13 ms. That is roughly the total
time measured (about 12 ms, 5–6×): everyone else finished and waited. Splitting by the first *two*
missing cards gives 990 tasks, the biggest under 1% of the work, and 16 threads then reached about
7×. The general lesson: the speedup can't beat total time ÷ the longest single task.

### Results, and why not 16×

| AsAh vs KsKh preflop, MSVC | 1 thread | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| Exact, 1,712,304 boards | 69 ms | 1.8× | 2.5× | 4.5× | 6.9× (10 ms) |
| Monte Carlo, 10M trials | 749 ms | 1.9× | 2.5× | 4.8× | 7.0× (107 ms) |

The laptop has 8 physical cores, each running two hardware threads (SMT, "hyper-threading"). Two
threads on one core share its execution units, which I measured by pinning 2-thread Monte Carlo runs
to particular CPUs:

| 2 threads, Monte Carlo 10M trials | MSVC | GCC |
|---|---|---|
| Same physical core | 1.3× | 1.5× |
| Two different cores | 1.8× | 1.9× |

Without pinning, Windows decides, and the GCC build's two threads often landed on the same core, so
its 2-thread results swung between about 1.4× and 2×. On top of that, a laptop runs one busy core at
a higher clock than eight busy cores (I didn't measure the clocks, but this is how its boost works).
Together these explain most of the gap between the ~7× measured and the 16× you might expect.

### A performance regression, found by bisection

After the threading change, single-threaded Monte Carlo on GCC was **25% slower** than before (about
250 ms instead of 200 ms for 3M trials), even though the per-trial work hadn't changed. Comparing
`main` and the branch side by side, one change at a time:

| Experiment | Time (main ≈ 200 ms) |
|---|---|
| The new code as written | ≈ 250 ms |
| `main` plus only the thread-library link | ≈ 200 ms: not the cause |
| Copying captured variables into locals | ≈ 300 ms: worse |
| Per-thread totals on the heap | ≈ 255 ms: no change |
| Loop called directly, without the helper | ≈ 340 ms: even worse |
| **Random generator on the heap** | **≈ 200 ms in every variant**, on both compilers |

The big swings from small, unrelated changes pointed at memory layout rather than extra work.
`std::mt19937_64` keeps **2.5 KB of state**, and it lived on the stack right next to the hot loop.
Every trial makes about a dozen function calls, each writing return addresses and saved registers to
the stack. The most likely mechanism is **4K aliasing**: when a load's address shares its low 12 bits
with a recent store to a different address, the CPU briefly assumes they might be the same and
stalls. Which parts of the generator's state collide depends on the exact stack layout, so
rearranging the code moved the problem around. I couldn't confirm it with hardware performance
counters on this machine, but moving only the generator to the heap removed the slowdown in every
variant, and over six alternating rounds the branch matched `main` (198.8 ms against 198.0 ms).

Lessons: compare against the previous version before calling an optimisation done, change one thing
at a time, and keep large objects off the stack frames of hot loops.

### Data races and ThreadSanitizer

A **data race** is two threads accessing the same memory without synchronisation, at least one of
them writing. The design avoids races by construction (local totals, read-only shared inputs, an
atomic counter, results passed back through `join()`), but that needs checking by a tool, not by
review. **ThreadSanitizer** (`-DPOKER_TSAN=ON`, GCC/Clang) instruments every memory access and
reports races. MSVC doesn't offer it and it can't be combined with AddressSanitizer, so CI runs it
as its own Linux job. The fast evaluator's lazily built tables are also exercised by 8 threads racing
to make the first call.

## 7. Ranges

Players can hold **ranges** like `QQ+, AKs, A5s-A2s` or `random` instead of known hands. A known
hand is just a range with one hand in it, so there is one code path for both, and known-hand
results are bit-identical to before ranges existed. I checked that by compiling one small program
against the old and the new library and comparing raw counts, exact and Monte Carlo, on 1 and 8
threads.

### Representing a range

There are C(52,2) = 1,326 possible two-card hands ("combos"). Each gets a number from its cards'
indices: with `hi > lo`, `index = hi·(hi − 1)/2 + lo`, which numbers every pair of cards exactly
once, in order of the higher card and then the lower one. A `Range` keeps:

- a **bitset** over those numbers, so "is this hand in the range?" is a single lookup;
- a **vector of the hands sorted by number**, the range's *canonical order*.

The canonical order matters for reproducibility: `"QQ+, AKs"` and `"AKs, AA, KK, QQ"` list exactly
the same hands in exactly the same order. Seeded Monte Carlo picks hands by their position in that
list, so it gives bit-identical results however the range is written. There's a test for that.

### Parsing the notation

Each comma-separated entry is a specific hand (`AhKh`), a *hand class*, or a range of classes:

- **A class** expands to its hands: a pair to 6 (one per pair of suits), a suited hand to 4, an
  offsuit hand to 12, both together to 16.
- **`+` and dash ranges** are loops over classes: `QQ+` is QQ, KK, AA, `ATs+` raises the kicker up
  to just below the ace, and `A5s-A2s` covers every kicker in between.
- **Errors** say which entry is wrong and why, so parsing returns
  `std::variant<Range, RangeError>`, where `RangeError` holds the entry and a reason (for example
  "AKx" and "the last letter must be 's' (suited) or 'o' (offsuit)").

A neat completeness test: every pair plus every two-rank class (`22+, A2+, K2+, …, 32`) must equal
`random`, because 78 pairs + 78 classes × 16 hands = 1,326.

### Exact range-vs-range equity

Exact enumeration now runs over every **deal** (one hand from each player's range, no card used
twice) combined with every board:

- **Why plain counting is still correct:** every deal uses exactly two cards per player, so every
  deal leaves the same number of cards to complete the board from, and every (deal, board) pair is
  equally likely. The integer totals simply add up across deals, with no weighting needed.
- **Threads:** the tasks become (deal, board prefix) pairs. A prefix is stored as two *deck
  positions* rather than two cards, because each deal leaves a different deck, but always one of
  the same size. Everything in section 6 then applies unchanged.
- **Cost:** AA vs KK is 36 deals × 1,712,304 boards ≈ 61.6 million showdowns, about 0.35 s on 16
  threads. `exactShowdowns()` computes that count up front (stopping once it passes a limit, since
  counting the deals of wide ranges takes time too), which is how the CLI chooses a method: exact
  up to 200 million showdowns, Monte Carlo beyond.

### Monte Carlo: rejection sampling

Each trial deals every player a hand from their range, then deals the board:

1. **Hands:** draw a hand for every player independently. If any two collide (share a card), throw
   the *whole* draw away and draw again. Every valid combination of hands is then exactly equally
   likely. Players with a single possible hand just keep it and use no random numbers, which is
   why known-hand results didn't change.
2. **Board:** the partial Fisher–Yates shuffle from section 5, but if it picks a card that's in a
   player's hand this trial, that pick is rejected and redrawn. Rejecting from a uniform choice over
   a bigger set leaves a uniform choice over the cards that are really free.

Because rejection sampling only finishes if some valid deal exists, the code first checks that one
does, and reports `kNoValidDeal` otherwise (for example `AA` vs `AA` vs `AA`: there are only four
aces).

**The trap:** the tempting shortcut is to draw each player's hand only from those that don't collide
with the hands already drawn. That is *biased*. Take player 1 with `AsAh, 4c6d` and player 2 with
`AsKs, AhQh, KdKc`. `AsAh` collides with two of player 2's three hands, so of the 4 valid deals only
one contains `AsAh`. With a river where player 1 wins only that deal, the true equity is exactly
1/4. The shortcut still picks `AsAh` half the time, and gets 1/2.

I mutation-tested this. With the shortcut swapped in, the test built for this case measured 0.4993
instead of 0.25, as predicted. Every *other* Monte Carlo test still passed, including the
comparisons with exact enumeration and with Wizard of Odds, because in those spots collisions are
rare or symmetric. Without a test aimed at the bias, the bug would have gone unnoticed.

### How we know it's right

| Check | Result |
|---|---|
| AA against one random hand, exact | Wins, losses and draws (ours for A♠A♥, × 6 for the 6 AA hands) equal the [Wizard of Odds' exhaustive table](https://wizardofodds.com/games/texas-hold-em/2-player-game/) **exactly**: 10,689,050,508 / 1,827,970,020 / 68,413,872 (an opt-in test, about 10 s) |
| AA and 72o against random, Monte Carlo | within 4 standard errors of the same table |
| AA vs KK range | exactly 6 × (1·c₂ + 4·c₁ + 1·c₀), where c₂, c₁, c₀ are the known-hand results for 2, 1 and 0 shared suits (section 8), as suit symmetry requires: 81.95% / 18.05% |
| AKs vs QQ range | exactly 12 × (shares a suit + shares none): 46.05% / 53.95% |
| One-hand ranges | the same counts *and errors* as the HoleCards versions |

## 8. Testing approach

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
and GCC builds print the same checksum. Monte Carlo relies on the same property: for a given seed it
deals the same boards, and prints the same numbers, on every platform.

## 9. Build and tooling notes

- **Sanitizers:** `add_compile_options()` only affects targets created *after* it. The original
  `CMakeLists.txt` added the sanitizer flags after `add_library(poker)`, so the library itself was
  never instrumented. The block now comes first.
- **MSVC has AddressSanitizer but no UBSan**, and ASan is incompatible with MSVC's default Debug
  runtime checks (`/RTC1`) and incremental linking, so `POKER_SANITIZE` handles MSVC separately and
  copies the ASan runtime DLL next to each executable.
- Warnings: the library builds cleanly with `/W4` (MSVC) and with
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wsign-conversion` (GCC 15).
- **No CPU-specific flags:** the builds don't use `-march=native`, `-mpopcnt` or similar, so the
  binaries run on any x86-64 CPU (and the code stays ready for WebAssembly). The price is that
  `std::popcount` can be a library call, which is why the fast evaluator avoids it in its hot path.
- **32-bit builds:** everything also builds warning-free and passes all tests as 32-bit (Win32),
  where `size_t` is 32 bits, as it will be for WebAssembly's wasm32 target.
- **ThreadSanitizer in CI:** recent Ubuntu kernels randomise memory addresses with more bits than
  TSan's memory layout expects, which can make TSan programs abort at startup ("unexpected memory
  mapping"). The CI job lowers the setting first (`sysctl vm.mmap_rnd_bits=28`), the usual
  workaround.

## 10. Quick answers to likely interview questions

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
  heads-up preflop, and it explodes for ranges, which is where Monte Carlo comes in.
- **How accurate is the Monte Carlo answer?** Its standard error is σ/√N, and it reports a 95%
  margin of ± 1.96 standard errors. That's about ±0.1% at 1M trials heads-up; halving the margin
  takes four times the trials.
- **Why not `rand() % n` or `std::uniform_int_distribution`?** Modulo is biased whenever 2⁶⁴ isn't a
  multiple of n, and the standard distribution isn't specified precisely enough to give the same
  numbers on every platform. Rejection sampling fixes both.
- **How do you test something random?** Fix the seed so every run is identical, then use statistics
  to choose the tolerance: compare with the exact answer within 4 standard errors, and check that 95%
  intervals really cover the truth about 95% of the time. Finally, plant a bug and confirm the tests
  catch it.
- **How does multithreading stay reproducible?** Each task's result depends only on its number
  (each Monte Carlo chunk has its own seeded random stream and fresh deck), and the totals are
  integers. The result is the same whichever thread runs which task, and in whatever order.
- **Why `std::variant` and not exceptions?** Invalid input is expected at an API boundary, the core
  library avoids exceptions, and the error enum lets callers react to the specific problem.
- **How does the fast evaluator work?** If a suit has 5+ cards, its 13-bit rank set indexes a flush
  table. Otherwise only the rank counts matter, and a perfect hash (each count vector's position in
  sorted order) indexes a 49,205-entry table. Every entry was filled in by the naive evaluator at
  startup, in about 6 ms.
- **How do you know the fast evaluator is right?** It's compared with the reference on every possible
  5-, 6- and 7-card hand (over 150 million), and the tables are checked to be exactly as big as the
  number of possibilities.
- **What surprised you while optimising?** Removing an early loop exit made it 1.6× faster, because a
  branch the CPU can't predict costs more than a few extra additions. And `std::popcount` turned out
  to be a library call on GCC without `-mpopcnt`. Measuring beat guessing both times.
- **Why not the Two Plus Two evaluator?** Its 130 MB table is slow to build and too big for the
  browser. Ours is 330 KB, fits in L2 cache, and is still about 9× faster than the reference.
- **How do the threads share the work?** An atomic counter hands out task numbers, and each thread
  takes the next one when it's ready. Each adds into its own local totals, combined after `join()`:
  no locks, and no false sharing.
- **What is false sharing?** Threads writing *different* variables that sit in the same 64-byte cache
  line. The caches have to pass the line back and forth between cores, so the threads slow each
  other down even though they never share data.
- **Why is `memory_order_relaxed` enough for the counter?** It only has to give each task to exactly
  one thread. The results become visible to the caller through `join()`, not through the counter.
- **Why ~7× on 16 hardware threads, not 16×?** There are only 8 physical cores, and two threads on
  one core get about 1.3–1.5×, not 2× (measured by pinning them). A laptop also runs fewer busy cores
  at higher clocks. And for exact enumeration, the speedup can't beat total time ÷ the biggest task,
  which is why it splits by the first two missing cards (990 tasks), not one (44 uneven tasks).
- **Why not a thread pool?** Starting threads costs well under a millisecond, small next to the work.
  A pool pays off for many tiny calls, and adds complexity.
- **Tell me about a hard bug.** Single-threaded Monte Carlo got 25% slower after the threading
  change, with no extra work per trial. Bisecting one change at a time ruled out the thread library,
  variable capture and where the totals lived. Moving the random generator's 2.5 KB state off the
  stack fixed it in every variant; the most likely cause was 4K aliasing between that state and
  the stack writes of the loop's function calls.
- **How do you represent a range?** A bitset over the 1,326 possible hands (each numbered from its
  two card indices) for instant membership, plus the hands in a sorted canonical order, so seeded
  results don't depend on how the range was written.
- **How is range-vs-range equity weighted?** Every deal of the hands leaves the same number of cards
  for the board, so every (deal, board) pair is equally likely and plain counting is correct.
- **How do you deal random hands from overlapping ranges?** Rejection sampling: draw all the hands,
  and redraw everything if two collide. Redrawing only the colliding player is biased toward hands
  that collide often; a dedicated test shows the bias (1/2 instead of 1/4), and only that test
  catches it.
- **How do you know the range code is right?** Exact AA-vs-any-hand counts match the Wizard of Odds'
  published enumeration to the last digit, range results equal suit-weighted sums of known-hand
  results exactly, and a planted sampling bug is caught.
- **What would you do next for speed?** Division-free random numbers for Monte Carlo (Lemire's
  method) and reusing the board's work across players in enumeration, always testing against the
  reference implementations.
