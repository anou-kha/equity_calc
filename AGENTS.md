# AGENTS.md

Instructions for AI coding agents working in this repository. Read these first; they are the
source of truth:

- `CLAUDE.md`: build and test commands, layout, conventions and constraints. It was written for
  Claude Code, but it applies to every agent.
- `docs/design.md`: how each component works and why, including the optimisations already measured
  (and what didn't help).

## The rules that matter most for performance work

- **Correctness first.** After every change, build and run the full test suite (`ctest`), plus both
  WebAssembly builds' tests if you touch `wasm/` or `web/`. Fix warnings: the library compiles with
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
- **Results must stay bit-identical**: for every thread count, between compilers, and between the
  native and WebAssembly builds (`wasm/smoke_test.mjs` compares them to the last digit). So no
  `-ffast-math`, `-march=native`, FMA or other CPU-specific flags, and no
  `std::uniform_int_distribution` or `rand() % n` (use `random.hpp`).
- **Keep the reference implementations** (`evaluateNaive()` in `evaluator.cpp`) unchanged: the fast
  evaluator's tables are built from it and tested against it on every hand.
- **Hot paths:** no heap allocation, no exceptions, no virtual calls.
- **Measure, don't guess.** Use `poker_bench` from a Release build: three runs before and three
  after, alternating, and compare medians (this machine's speed varies from run to run). Report
  the numbers; don't claim a speedup you haven't measured.
- Format with `.clang-format` (100 columns). Small, focused commits with descriptive messages.
  Don't push or open pull requests: the owner does that.
- On Windows (PowerShell 5.1, Visual Studio generator): `cmake --build build --config Release` and
  `ctest --test-dir build -C Release`; executables are in `build\Release\`.
