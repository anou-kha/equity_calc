# CLAUDE.md — project context for Claude Code

## What this is
A high-performance Texas Hold'em **equity calculator** in modern C++ (C++20). It computes each
player's probability of winning/tying given hole cards (or ranges) and an optional board, using
exact enumeration where feasible and Monte Carlo simulation otherwise. It is a portfolio project
for SWE internship applications, so **code quality, tests, benchmarks and clear docs matter as
much as features.**

## I am learning
I'm a student building this to learn. When you implement something non-trivial (an evaluator
algorithm, a lookup table, a concurrency pattern), briefly explain the idea and the trade-offs in
your reply and in a short code comment, so I can explain it in an interview. Prefer clear code
over clever code unless we're explicitly optimising, and when we optimise, keep the slower
reference version around for tests and benchmark comparisons.

## Build & test
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/poker_bench
```
Debug with sanitizers: `cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DPOKER_SANITIZE=ON`
(ASan + UBSan on GCC/Clang; ASan only on MSVC). On Windows with the Visual Studio generator, pass
`--config Release` to the build and `-C Release` to ctest; see README.md.

WebAssembly (Emscripten 7.0.1, activated with `emsdk_env`; add `-G Ninja` on Windows):
```bash
emcmake cmake -S . -B build-wasm-threads -DCMAKE_BUILD_TYPE=Release -DPOKER_WASM_THREADS=ON
cmake --build build-wasm-threads --parallel && ctest --test-dir build-wasm-threads   # suite, in Node
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel && ctest --test-dir build-wasm   # module, worker, page (Chrome)
```
The browser build assembles the web page in `build-wasm/site`; serve that folder to try it.

Always run the build and the full test suite after a change, and fix warnings: the library is
compiled with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`. After changing `web/` or `wasm/`,
also run both WebAssembly builds' tests.

## Layout
- `include/poker/` — public headers (namespace `poker`)
- `src/` — library implementation + `main.cpp` (CLI)
- `tests/` — GoogleTest unit tests (fetched via CMake FetchContent)
- `bench/` — benchmark executable(s), plain `std::chrono`, no external deps
- `wasm/` — the browser module's JavaScript binding (embind) and its Node smoke test
- `web/` — the web page: plain HTML/CSS/JS, a Web Worker, and their tests (Node, headless Chrome)
- `docs/` — design notes, benchmark results

## Conventions
- C++20, no compiler extensions. Standard library only for the core library (no Boost).
- Format with `.clang-format` (Google-based, 100 cols). CI fails on unformatted code.
- Card encoding is fixed: `index = rank * 4 + suit`, rank 0='2'…12='A', suit 0..3 = c,d,h,s.
  Card sets are `CardMask` (`uint64_t`, bit i = card index i).
- Hot paths: no heap allocation, no exceptions, no virtual calls. Use `std::optional` for parse
  errors at API boundaries; when the caller needs to know *why* input is invalid, return
  `std::variant<Result, ErrorEnum>` (see `exactEquity`).
- Every new component gets unit tests. Equity results must be checked against known reference
  values (e.g. AA vs KK preflop ≈ 81.9% / 18.1%; AKs vs QQ ≈ 46% / 54%).
- Monte Carlo code takes an explicit seed so tests are deterministic. For randomness use
  `poker::Rng`, `uniformBelow` and `mixSeed` (`random.hpp`), never `std::uniform_int_distribution`
  or `rand() % n`: the standard distributions give different numbers on different standard
  libraries, and plain modulo is biased.
- Hand evaluation: `evaluate()` (lookup tables, `fast_evaluator.cpp`) must always return exactly
  what `evaluateNaive()` (the reference, `evaluator.cpp`) returns; the tests compare them on every
  hand. The fast evaluator's tables are built *from* `evaluateNaive()`, so treat changes to the
  reference with great care, and use `evaluate()` everywhere else.
- Multithreading: use `detail::runInParallel` (`src/parallel.hpp`). Results must be bit-identical
  for every thread count: split work into tasks that depend only on their number (e.g. Monte Carlo
  chunk c always uses `mixSeed(seed, c)`), keep integer totals per thread, and combine them at
  the end. The library defaults to 1 thread; only the CLI uses all cores by default. Keep large
  objects (like `std::mt19937_64`'s 2.5 KB state) off the stack in hot loops, see design.md.
- Ranges: a known hand is a one-hand `Range`, and must give exactly the same results and errors
  as the HoleCards API. Keep a range's hands in canonical order (so seeded results don't depend on
  how the range was written), and deal Monte Carlo hands by rejection sampling of the whole deal:
  never redraw only the colliding player, which is biased (see `EveryValidDealIsEquallyLikely`).
- Front ends go through `calculate()` (`calculator.hpp`): the CLI and the WebAssembly binding only
  convert input and output, and both use its `toJson()`, so they can't disagree. New options go
  into `CalculationRequest`, with unit tests and, for the CLI, an end-to-end `poker_cli_test()`.
- WebAssembly: the browser module is single-threaded (threads would need COOP/COEP headers that
  GitHub Pages can't send) and runs in a Web Worker. Its results must match the native build to
  the last digit; `wasm/smoke_test.mjs` holds native reference values, so regenerate them with
  `poker_cli ... --json` if a result legitimately changes.
- The web page shows user input with `textContent`, never `innerHTML` (`web/browser_test.mjs`
  checks this). No frameworks or build step; new page files go into `POKER_WEB_FILES` in
  CMakeLists.txt.
- Add new source files to `CMakeLists.txt` explicitly (no globbing).
- Small, focused commits with descriptive messages.

## Roadmap
1. ✅ Card representation & parsing
2. ✅ Naive 5/7-card hand evaluator (reference implementation)
3. ✅ Exact equity via enumeration (heads-up and multiway, with partial boards)
4. ✅ Monte Carlo equity with seeded RNG + confidence interval reporting
5. ✅ Fast evaluator (lookup tables / perfect hashing) — benchmark vs naive
6. ✅ Multithreaded Monte Carlo (and exact enumeration)
7. ✅ Range parsing (`AKs, QQ+, A5s-A2s, 22+`) and range-vs-range equity
8. ✅ CLI polish, then WebAssembly build + web frontend (version 1.0.0)
