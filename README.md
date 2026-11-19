# poker-equity

A fast Texas Hold'em equity calculator written in modern C++20.

> 🚧 Work in progress — see the roadmap below.

## Features (planned)
- 7-card hand evaluator (naive reference + optimised lookup-table version)
- Exact equity by enumeration and Monte Carlo equity with confidence intervals
- Multithreaded simulation
- Range notation (`AKs, QQ+, A5s-A2s`) and range-vs-range equity
- CLI, and a WebAssembly build for the browser

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (GCC 11+, Clang 14+, or MSVC 2022).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Usage

```bash
./build/poker_cli AsKs QhQd        # (currently just parses and echoes cards)
./build/poker_bench
```

## Benchmarks

_Coming soon: hands evaluated per second, naive vs optimised evaluator._

| Evaluator | Hands/sec | Speedup |
|-----------|-----------|---------|
| Naive     | –         | 1×      |
| Optimised | –         | –       |

## Project layout

```
include/poker/   public headers
src/             library + CLI
tests/           GoogleTest unit tests
bench/           benchmarks
docs/            design notes
```

## Roadmap
- [x] Card representation & parsing
- [ ] Naive hand evaluator
- [ ] Exact equity (enumeration)
- [ ] Monte Carlo equity
- [ ] Fast evaluator + benchmarks
- [ ] Multithreading
- [ ] Range parsing & range-vs-range equity
- [ ] WebAssembly build + web UI
