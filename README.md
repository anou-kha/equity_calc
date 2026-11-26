# poker-equity

A fast Texas Hold'em equity calculator written in modern C++20.

> 🚧 Work in progress — see the roadmap below.

## Features
- ✅ 7-card hand evaluator: a lookup-table evaluator (80–90 M hands/s) built from, and checked
  against, a simple reference implementation on every one of the 133,784,560 seven-card hands
- ✅ Exact equity by enumeration: 2–9 players, preflop/flop/turn/river, dead cards, exact split pots
- ✅ Monte Carlo equity with 95% confidence intervals; seeded, so results are reproducible on every
  platform
- ✅ Multithreaded exact and Monte Carlo equity (about 7× faster on 8 cores), with results that are
  bit-identical for any number of threads
- ✅ Hand ranges (`QQ+, AKs, A5s-A2s, random`) and range-vs-range equity, exact or Monte Carlo;
  verified exactly against the Wizard of Odds' published hand-vs-random table
- ✅ Command-line interface that picks exact or Monte Carlo automatically
- Planned: WebAssembly build and web UI

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (GCC 11+, Clang 14+, or MSVC 2022+).

**Linux / macOS**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

**Windows (PowerShell, Visual Studio generator)**

Visual Studio is a multi-config generator: choose the configuration when building and testing,
and executables end up in `build\Release\`.
```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

**Sanitizers:** configure a Debug build with `-DPOKER_SANITIZE=ON`. With GCC/Clang this enables
AddressSanitizer and UndefinedBehaviorSanitizer; MSVC only supports AddressSanitizer. A separate
`-DPOKER_TSAN=ON` build (GCC/Clang only) enables ThreadSanitizer, which detects data races; CI runs
it on Linux.

**Slow tests:** a few tests are disabled by default because they take seconds even in Release:
checking every 6- and 7-card hand (the known category frequencies for both evaluators, and that the
fast evaluator matches the reference on every hand), and an exact AA-against-any-hand enumeration
of 2.1 billion showdowns that must reproduce the Wizard of Odds' published counts. Run them with:
```bash
./build/poker_tests --gtest_also_run_disabled_tests --gtest_filter=*AllSevenCard*:FastEvaluator.*:*WizardOfOdds*
```

## Usage

Each player is a **range** of possible hands; a known hand like `AsKs` is simply a range of one.

```
$ poker_cli AsKs QhQd --board Js Ts 2h
Board: Js Ts 2h

Range   Hands       Win       Tie    Equity
AsKs        1    56.06%     0.00%    56.06%
QhQd        1    43.94%     0.00%    43.94%

990 boards enumerated in 0.199 ms on 1 thread
```

```
$ poker_cli AA KK
Board: (none)

Range   Hands       Win       Tie    Equity
AA          6    81.71%     0.46%    81.95%
KK          6    17.82%     0.46%    18.05%

61,642,944 showdowns (36 deals of the hands x 1,712,304 boards) enumerated in 347.5 ms on 16 threads
```

| Range notation | Means | Hands |
|---|---|---|
| `QQ` | a pair | 6 |
| `AKs` / `AKo` / `AK` | suited / offsuit / both | 4 / 12 / 16 |
| `QQ+` | QQ, KK, AA | 18 |
| `ATs+` | ATs, AJs, AQs, AKs (the kicker climbs) | 16 |
| `JJ-99`, `A5s-A2s` | everything in between | 18, 16 |
| `AhKh` | one specific hand | 1 |
| `random` (or `any`) | any two cards | 1,326 |

Combine entries with commas, and quote a range that contains spaces: `"QQ+, AKs"` (or write it
without spaces, `QQ+,AKs`). Hands that would use a board or dead card are left out automatically.

**Exact or Monte Carlo:** the answer is exact when that takes at most 200 million showdowns
(about a second on 8 cores). Otherwise the CLI deals 10 million random hands and boards and reports
a 95% margin of error, which shrinks like 1/√n: four times the trials halve it.

```
$ poker_cli "QQ+, AKs" random
Board: (none)

Range      Hands       Win       Tie    Equity    95% margin
QQ+, AKs      22    79.31%     0.76%    79.70%     +/- 0.02%
random      1326    19.92%     0.76%    20.30%     +/- 0.02%

Monte Carlo: 10,000,000 random deals (seed 1) in 134.4 ms on 16 threads
Exact enumeration would take over 200,000,000 showdowns; --exact forces it.
The true equity is within the margin for 95% of seeds.
```

- `--exact` forces exact enumeration (of up to 4,194,304 deals of the hands); `--trials <n>` asks
  for Monte Carlo with `n` trials, and `--seed <n>` picks its random stream (default 1). The same
  command and seed always print the same numbers.
- `--board` takes 0, 3, 4 or 5 cards; `--dead` takes cards that can't be dealt (e.g. folded or
  exposed). Both read every following argument up to the next option, so `Js Ts 2h` and `JsTs2h`
  both work.
- **Equity** is the expected share of the pot: wins plus an equal share of every split pot.
- The work is shared between all hardware threads; `--threads <n>` limits it (`--threads 1` for a
  single thread). Small jobs like the flop above use one thread, because starting threads would
  cost more than it saves. The number of threads never changes the answer.

See [docs/design.md](docs/design.md) for how all of this works. Run `poker_bench` (from a Release
build) for the benchmarks below.

## Benchmarks

Measured on an AMD Ryzen 9 5900HS laptop (8 cores, 16 hardware threads), Windows 11, Release
builds. Each figure is the median of three runs of `poker_bench`, which reports the fastest of
several passes; separate runs vary by up to about 8% (more for the short multithreaded timings).
The first two tables are single-threaded.

| Benchmark | MSVC 19.52 (`/O2`) | GCC 15.1 (`-O3`, MSYS2) |
|-----------|-------------------:|------------------------:|
| Naive evaluator, 1M random 7-card hands | 9.7 M hands/s | 9.5 M hands/s |
| Fast evaluator, the same hands | 80 M hands/s | 93 M hands/s |
| Exact equity, AsAh vs KsKh preflop (1,712,304 boards) | 69 ms | 50 ms |
| Monte Carlo, same spot, 1M trials (82.61% ± 0.07%) | 74 ms | 62 ms |
| Building the fast evaluator's tables (once) | 5.8 ms | 5.2 ms |

| Evaluator | Hands/sec (MSVC) | Speedup |
|-----------|-----------------:|--------:|
| Naive (reference) | 9.7 M | 1× |
| Fast (lookup tables) | 80 M | 8.3× |

With the naive evaluator, the exact preflop spot took about 230 ms (MSVC) and 1M Monte Carlo
trials about 240 ms, so equity got about 3× faster: less than the evaluator's 8–10×, because
enumerating boards and drawing random numbers now take a large share of the time.

**Multithreading** (AsAh vs KsKh preflop, MSVC). Every thread count gives exactly the same result.

| Threads | Exact, 1,712,304 boards | Monte Carlo, 10M trials |
|--------:|------------------------:|------------------------:|
| 1  | 69 ms | 749 ms |
| 2  | 39 ms (1.8×) | 392 ms (1.9×) |
| 4  | 28 ms (2.5×) | 298 ms (2.5×) |
| 8  | 16 ms (4.5×) | 157 ms (4.8×) |
| 16 | 10 ms (6.9×) | 107 ms (7.0×) |

With GCC, 16 threads take 12 ms (4.1×) and 94 ms (7.0×). The speedup is below the thread count
because there are only 8 physical cores (two hardware threads on one core share it, giving about
1.3–1.5× rather than 2×), and a laptop runs fewer busy cores at higher clock speeds. See
[docs/design.md](docs/design.md) for the measurements behind this.

**Ranges** (16 threads):

| Benchmark | MSVC | GCC |
|-----------|-----:|----:|
| Exact AA vs KK: 36 deals x 1,712,304 boards = 61.6M showdowns | 358 ms (172 M/s) | 261 ms (236 M/s) |
| Monte Carlo `QQ+, AKs` vs any two cards, 10M trials | 136 ms | 116 ms |

## Project layout

```
include/poker/   public headers: card, evaluator, equity, monte_carlo, random, range (.hpp)
src/             library + CLI (main.cpp)
tests/           GoogleTest unit tests
bench/           benchmarks
docs/            design notes (see docs/design.md)
```

## Roadmap
- [x] Card representation & parsing
- [x] Naive hand evaluator
- [x] Exact equity (enumeration)
- [x] Monte Carlo equity
- [x] Fast evaluator + benchmarks
- [x] Multithreading
- [x] Range parsing & range-vs-range equity
- [ ] WebAssembly build + web UI
