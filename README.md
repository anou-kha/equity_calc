# poker-equity

A fast Texas Hold'em equity calculator written in modern C++20, for the command line and, compiled
to WebAssembly, for the browser.

**Try it in your browser: https://anou-kha.github.io/equity_calc/**

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
- ✅ Command-line interface that picks exact or Monte Carlo automatically, with `--json` output
- ✅ WebAssembly build: the whole test suite also runs as WebAssembly, and the browser version
  gives the same numbers as the native one, to the last digit
- ✅ A web page (plain HTML, CSS and JavaScript) that runs the calculator in a Web Worker, tested
  end to end in headless Chrome and deployed with GitHub Pages

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

### WebAssembly and the web page

Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (CI uses 7.0.1)
and activate it in your shell (`source ./emsdk_env.sh`, or `emsdk_env.ps1` in PowerShell). There
are two WebAssembly builds:

```bash
# 1. The whole test suite, the CLI and the benchmarks as WebAssembly with threads, run by Node.js
emcmake cmake -S . -B build-wasm-threads -DCMAKE_BUILD_TYPE=Release -DPOKER_WASM_THREADS=ON
cmake --build build-wasm-threads --parallel
ctest --test-dir build-wasm-threads --output-on-failure

# 2. The browser module (single-threaded) and the web page, assembled in build-wasm/site
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
ctest --test-dir build-wasm --output-on-failure   # tests the page in headless Chrome if installed
python -m http.server --directory build-wasm/site 8000   # then open http://localhost:8000
```

Browsers only run WebAssembly with threads on pages served with special headers, which static
hosts like GitHub Pages can't send, so the browser module is single-threaded and runs in a Web
Worker. On Windows, add `-G Ninja`: Visual Studio's generator can't drive Emscripten, and Visual
Studio installs Ninja (a Developer PowerShell has it on PATH). See [docs/design.md](docs/design.md)
for the details.

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
- `poker_cli --help` lists everything, `--version` prints the version, and errors exit with
  status 1.

**JSON for scripts:** `--json` prints the result (or the error) as one line of JSON, the same JSON
the web page receives, made by the same code:

```
$ poker_cli AsKs QhQd --board Js Ts 2h --json
{"ok":true,"method":"exact","automatic":false,"board":"Js Ts 2h","dead":"","knownHands":true,
 "showdowns":990,"deals":1,"seed":null,"threads":1,"milliseconds":0.2513,"players":[
 {"range":"AsKs","hands":1,"win":0.5606060606060606,"tie":0,"equity":0.5606060606060606,"margin95":null},
 {"range":"QhQd","hands":1,"win":0.4393939393939394,"tie":0,"equity":0.4393939393939394,"margin95":null}]}

$ poker_cli AsKs "QQ+, AKx" --json
{"ok":false,"field":"range","player":1,"message":"invalid range \"QQ+, AKx\" at \"AKx\": the last letter must be 's' (suited) or 'o' (offsuit)"}
```

(The first output is wrapped here for reading; it's a single line.) Fractions are written in the
shortest form that reads back as exactly the same number, and `margin95` is only set for Monte
Carlo.

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

**WebAssembly vs native** (the same `poker_bench`, built with Emscripten 7.0.1 and run by Node.js
24, against the MSVC build; medians of three runs, alternating between the two, in one session):

| Benchmark | Native (MSVC) | WebAssembly | WebAssembly speed |
|-----------|--------------:|------------:|------------------:|
| Fast evaluator, 1M random 7-card hands | 63.9 M hands/s | 57.3 M hands/s | 90% |
| Exact equity, AsAh vs KsKh preflop, 1 thread | 83 ms | 109 ms | 77% |
| Monte Carlo, same spot, 1M trials, 1 thread | 99 ms | 140 ms | 70% |
| Exact AA vs KK (61.6M showdowns), 16 threads | 329 ms | 402 ms | 82% |
| Naive evaluator, 1M random 7-card hands | 7.4 M hands/s | 9.7 M hands/s | 131% |

Both builds produce the same checksums and the same equity, to the last digit. The browser page runs
single-threaded, at about 16 million heads-up showdowns a second, so it enumerates exactly up to 10
million showdowns (under a second) and uses Monte Carlo beyond that. (The native figures in this
table are lower than in the tables above, which were measured on another day: a laptop's speed
varies with temperature and power settings, so only figures from the same session are compared.)

## Project layout

```
include/poker/   public headers: calculator, card, evaluator, equity, monte_carlo, random, range
src/             library + CLI (main.cpp)
tests/           GoogleTest unit tests, and the CLI's end-to-end tests
bench/           benchmarks
wasm/            the JavaScript binding for the browser module, and its smoke test
web/             the web page (HTML, CSS, JavaScript, Web Worker) and its tests
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
- [x] CLI polish, WebAssembly build + web UI (version 1.0.0)
