# poker-equity

A fast Texas Hold'em equity calculator written in modern C++20.

> 🚧 Work in progress — see the roadmap below.

## Features
- ✅ 7-card hand evaluator (naive reference implementation, verified against all 133,784,560
  seven-card hands)
- ✅ Exact equity by enumeration: 2–9 players, preflop/flop/turn/river, dead cards, exact split pots
- ✅ Monte Carlo equity with 95% confidence intervals; seeded, so results are reproducible on every
  platform
- ✅ Command-line interface
- Planned: optimised lookup-table evaluator, multithreading, range notation (`AKs, QQ+, A5s-A2s`)
  and range-vs-range equity, WebAssembly build

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
AddressSanitizer and UndefinedBehaviorSanitizer; MSVC only supports AddressSanitizer.

**Slow tests:** one test checks all 133,784,560 seven-card hands and is disabled by default
(about 10 s in Release). Run it explicitly with:
```bash
./build/poker_tests --gtest_also_run_disabled_tests --gtest_filter=*AllSevenCard*
```

## Usage

```
$ poker_cli AsKs QhQd --board Js Ts 2h
Board: Js Ts 2h

Hand           Win       Tie    Equity
AsKs        56.06%     0.00%    56.06%
QhQd        43.94%     0.00%    43.94%

990 boards enumerated in 0.1 ms
```

- Give each player's two hole cards as one argument (`AsKs` or `"As Ks"`), for 2–9 players.
- `--board` takes 0, 3, 4 or 5 cards; `--dead` takes cards that can't be dealt (e.g. folded or
  exposed). Both read every following argument up to the next option, so `Js Ts 2h` and `JsTs2h`
  both work.
- **Equity** is the expected share of the pot: wins plus an equal share of every split pot.

```
$ poker_cli AsAh KsKh
Board: (none)

Hand           Win       Tie    Equity
AsAh        82.36%     0.54%    82.64%
KsKh        17.09%     0.54%    17.36%

1712304 boards enumerated in 266.8 ms
```

**Monte Carlo:** add `--trials <n>` to deal that many random boards instead of enumerating them
all. The answer then comes with a 95% margin of error, which shrinks like 1/√n: four times the
trials halve it. `--seed <n>` picks the random stream (default 1), and the same command and seed
always print the same numbers.

```
$ poker_cli AsKs QhQd --board Js Ts 2h --trials 1000000
Board: Js Ts 2h

Hand           Win       Tie    Equity    95% margin
AsKs        56.09%     0.00%    56.09%     +/- 0.10%
QhQd        43.91%     0.00%    43.91%     +/- 0.10%

Monte Carlo: 1000000 random boards (seed 1) in 156.9 ms
The true equity is within the margin for 95% of seeds.
```

For known hole cards like these, exact enumeration is about as fast and has no error at all, so
Monte Carlo is mostly useful once ranges arrive (see the roadmap). See
[docs/design.md](docs/design.md) for how both methods work.

Run `poker_bench` (from a Release build) for the benchmarks below.

## Benchmarks

Baseline numbers for the naive reference evaluator, measured on an AMD Ryzen 9 5900HS laptop
(8 cores, single-threaded), Windows 11. Each figure is the fastest of several passes within a run;
separate runs varied by up to about 8%. The CLI example above is a single cold run, so it is slower.

| Benchmark | MSVC 19.52 (`/O2`) | GCC 15.1 (`-O3`, MSYS2) |
|-----------|-------------------:|------------------------:|
| Naive evaluator, 1M random 7-card hands | 8.8 M hands/s | 8.6 M hands/s |
| Exact equity, AsAh vs KsKh preflop (1,712,304 boards) | 230 ms | 216 ms |
| Monte Carlo, same spot, 1M trials (82.61% ± 0.07%) | 240 ms | 225 ms |

| Evaluator | Hands/sec | Speedup |
|-----------|-----------|---------|
| Naive     | 8.8 M     | 1×      |
| Optimised | –         | –       |

## Project layout

```
include/poker/   public headers: card.hpp, evaluator.hpp, equity.hpp, monte_carlo.hpp, random.hpp
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
- [ ] Fast evaluator + benchmarks
- [ ] Multithreading
- [ ] Range parsing & range-vs-range equity
- [ ] WebAssembly build + web UI
