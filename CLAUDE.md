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

Always run the build and the full test suite after a change, and fix warnings: the library is
compiled with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.

## Layout
- `include/poker/` — public headers (namespace `poker`)
- `src/` — library implementation + `main.cpp` (CLI)
- `tests/` — GoogleTest unit tests (fetched via CMake FetchContent)
- `bench/` — benchmark executable(s), plain `std::chrono`, no external deps
- `docs/` — design notes, benchmark results

## Conventions
- C++20, no compiler extensions. Standard library only for the core library (no Boost).
- Format with `.clang-format` (Google-based, 100 cols). CI fails on unformatted code.
- Card encoding is fixed: `index = rank * 4 + suit`, rank 0='2'…12='A', suit 0..3 = c,d,h,s.
  Card sets are `CardMask` (`uint64_t`, bit i = card index i).
- Hot paths: no heap allocation, no exceptions, no virtual calls. Use `std::optional` for parse
  errors at API boundaries.
- Every new component gets unit tests. Equity results must be checked against known reference
  values (e.g. AA vs KK preflop ≈ 81.9% / 18.1%; AKs vs QQ ≈ 46% / 54%).
- Monte Carlo code takes an explicit seed so tests are deterministic.
- Add new source files to `CMakeLists.txt` explicitly (no globbing).
- Small, focused commits with descriptive messages.

## Roadmap
1. ✅ Card representation & parsing
2. Naive 5/7-card hand evaluator (reference implementation)
3. Exact equity via enumeration (heads-up and multiway, with partial boards)
4. Monte Carlo equity with seeded RNG + confidence interval reporting
5. Fast evaluator (lookup tables / perfect hashing) — benchmark vs naive
6. Multithreaded Monte Carlo
7. Range parsing (`AKs, QQ+, A5s-A2s, 22+`) and range-vs-range equity
8. CLI polish, then WebAssembly build + web frontend
