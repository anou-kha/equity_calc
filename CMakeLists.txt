cmake_minimum_required(VERSION 3.20)
project(poker_equity VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(POKER_BUILD_TESTS "Build unit tests" ON)
option(POKER_BUILD_BENCH "Build benchmarks" ON)
option(POKER_SANITIZE "Enable address/undefined sanitizers" OFF)
option(POKER_TSAN "Enable ThreadSanitizer (GCC/Clang only; can't be combined with POKER_SANITIZE)"
  OFF)

# ---- Sanitizers ------------------------------------------------------------
# This must come before any target is created: add_compile_options() only applies to targets
# defined after it.
if(POKER_SANITIZE)
  if(MSVC)
    # MSVC supports AddressSanitizer but not UBSan. ASan is incompatible with the /RTC1 runtime
    # checks and with incremental linking, which CMake enables by default in Debug.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_DEBUG
      "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
    add_compile_options(/fsanitize=address)
    # The ASan runtime is a DLL next to cl.exe that is not on PATH outside a Developer Command
    # Prompt, so poker_copy_asan_runtime() copies it next to each executable.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(POKER_ASAN_ARCH x86_64)
    else()
      set(POKER_ASAN_ARCH i386)
    endif()
    get_filename_component(POKER_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(POKER_ASAN_DLLS
      "${POKER_MSVC_BIN_DIR}/clang_rt.asan_dynamic-${POKER_ASAN_ARCH}.dll"
      "${POKER_MSVC_BIN_DIR}/clang_rt.asan_dbg_dynamic-${POKER_ASAN_ARCH}.dll"
    )
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ThreadSanitizer detects data races: two threads touching the same memory without
# synchronisation, at least one of them writing. It needs its own build because it can't be
# combined with AddressSanitizer, and MSVC doesn't provide it.
if(POKER_TSAN)
  if(MSVC)
    message(FATAL_ERROR "POKER_TSAN: ThreadSanitizer isn't available with MSVC; use GCC or Clang")
  endif()
  if(POKER_SANITIZE)
    message(FATAL_ERROR "POKER_TSAN can't be combined with POKER_SANITIZE (ASan and TSan conflict)")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

# ---- WebAssembly (Emscripten) ----------------------------------------------
# Configured with `emcmake cmake ...`, this file makes one of two WebAssembly builds:
#  - By default, the browser module: the library plus its JavaScript binding (wasm/), without
#    threads. Threaded WebAssembly needs SharedArrayBuffer even to run one thread, and browsers
#    only allow that on pages served with special headers (COOP/COEP) that hosts like GitHub Pages
#    can't send.
#  - With POKER_WASM_THREADS=ON, the tests, CLI and benchmarks, with threads, run by Node.js
#    (which allows SharedArrayBuffer), so the whole test suite, multithreading included, runs as
#    WebAssembly.
option(POKER_WASM_THREADS "Emscripten: build the tests, CLI and benchmarks, with threads" OFF)
set(POKER_BUILD_PROGRAMS ON)  # the CLI, tests and benchmarks
if(EMSCRIPTEN)
  if(POKER_WASM_THREADS)
    # Every object file must be compiled with -pthread (GoogleTest's too), so this comes before
    # any target is created.
    add_compile_options(-pthread)
    # PROXY_TO_PTHREAD runs main() on a worker thread. A new thread only starts once Node's main
    # thread gets back to its event loop, so a main() running there and waiting for its threads
    # would wait forever. EXIT_RUNTIME makes main()'s return value the process's exit code.
    add_link_options(-pthread -sPROXY_TO_PTHREAD -sEXIT_RUNTIME)
  else()
    set(POKER_BUILD_PROGRAMS OFF)  # they use threads
  endif()
elseif(POKER_WASM_THREADS)
  message(FATAL_ERROR "POKER_WASM_THREADS is for Emscripten builds (emcmake cmake ...)")
endif()

function(poker_copy_asan_runtime target)
  foreach(dll IN LISTS POKER_ASAN_DLLS)
    if(EXISTS "${dll}")
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:${target}>")
    endif()
  endforeach()
endfunction()

# ---- Core library ----------------------------------------------------------
add_library(poker
  src/calculator.cpp
  src/card.cpp
  src/equity.cpp
  src/evaluator.cpp
  src/fast_evaluator.cpp
  src/monte_carlo.cpp
  src/random.cpp
  src/range.cpp
  src/showdown.cpp
)
target_include_directories(poker PUBLIC include)
find_package(Threads REQUIRED)  # exact and Monte Carlo equity can use several std::threads
target_link_libraries(poker PUBLIC Threads::Threads)
target_compile_options(poker PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
target_compile_definitions(poker PRIVATE POKER_VERSION="${PROJECT_VERSION}")  # poker::version()

# ---- Browser module (WebAssembly) ------------------------------------------
if(EMSCRIPTEN AND NOT POKER_WASM_THREADS)
  add_executable(poker_wasm wasm/bindings.cpp)
  target_link_libraries(poker_wasm PRIVATE poker)
  target_compile_options(poker_wasm PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  # -lembind: Emscripten's C++ <-> JavaScript bindings. MODULARIZE + EXPORT_ES6: the output,
  # poker_wasm.js, is an ES module whose default export, createPokerModule(), loads poker_wasm.wasm
  # and resolves to the API. FILESYSTEM=0 leaves out the emulated file system the library doesn't
  # use.
  target_link_options(poker_wasm PRIVATE
    -lembind -sMODULARIZE -sEXPORT_ES6 -sEXPORT_NAME=createPokerModule
    -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH -sFILESYSTEM=0)
  # Node only loads a .js file as an ES module inside a package marked "type": "module".
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/package.json" "{ \"type\": \"module\" }\n")

  # The web page (web/) and the module together in <build>/site, ready to serve as static files.
  set(POKER_WEB_FILES web/index.html web/style.css web/app.js web/worker.js web/page_module.js
    web/range_picker.js)
  list(TRANSFORM POKER_WEB_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
  add_custom_target(poker_site ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory site
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${POKER_WEB_FILES}
      $<TARGET_FILE:poker_wasm> $<TARGET_FILE_DIR:poker_wasm>/poker_wasm.wasm site
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Assembling the web page in site/"
    VERBATIM)
  add_dependencies(poker_site poker_wasm)

  enable_testing()
  add_test(NAME wasm.BrowserModule
    COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_CURRENT_SOURCE_DIR}/wasm/smoke_test.mjs
      $<TARGET_FILE:poker_wasm>)
  add_test(NAME wasm.WebWorker
    COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_CURRENT_SOURCE_DIR}/web/worker_test.mjs
      ${CMAKE_CURRENT_BINARY_DIR}/site)

  # The page itself, in headless Chrome, if it's installed (it is on GitHub's Ubuntu runners).
  find_program(POKER_CHROME
    NAMES google-chrome google-chrome-stable chromium chromium-browser chrome
    PATHS "C:/Program Files/Google/Chrome/Application"
      "$ENV{LOCALAPPDATA}/Google/Chrome/Application")
  if(POKER_CHROME)
    add_test(NAME wasm.WebPage
      COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_CURRENT_SOURCE_DIR}/web/browser_test.mjs
        ${POKER_CHROME} ${CMAKE_CURRENT_BINARY_DIR}/site)
  else()
    message(STATUS "Chrome not found: the web page's browser test (wasm.WebPage) is skipped")
  endif()
endif()

# ---- CLI -------------------------------------------------------------------
if(POKER_BUILD_PROGRAMS)
  add_executable(poker_cli src/main.cpp)
  target_link_libraries(poker_cli PRIVATE poker)
  poker_copy_asan_runtime(poker_cli)
endif()

# ---- Tests -----------------------------------------------------------------
if(POKER_BUILD_TESTS AND POKER_BUILD_PROGRAMS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(poker_tests
    tests/test_calculator.cpp
    tests/test_card.cpp
    tests/test_equity.cpp
    tests/test_evaluator.cpp
    tests/test_fast_evaluator.cpp
    tests/test_monte_carlo.cpp
    tests/test_multithreading.cpp
    tests/test_random.cpp
    tests/test_range.cpp
    tests/test_range_equity.cpp
  )
  target_link_libraries(poker_tests PRIVATE poker GTest::gtest_main)
  # Must run before gtest_discover_tests' own post-build step, which launches the executable.
  poker_copy_asan_runtime(poker_tests)
  include(GoogleTest)
  gtest_discover_tests(poker_tests)

  # End-to-end tests of the real poker_cli: run it with some arguments, then check the exit code
  # and that the output matches a CMake regular expression (in which [.] is a literal dot). In a
  # WebAssembly build, the CLI is run by Node.js (the "cross-compiling emulator").
  #   poker_cli_test(<name> <expected exit code> <pattern> <arguments...>)
  function(poker_cli_test name exit_code pattern)
    string(JOIN "|" command ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:poker_cli> ${ARGN})
    add_test(NAME cli.${name}
      COMMAND ${CMAKE_COMMAND} -DCOMMAND=${command} -DEXIT_CODE=${exit_code}
        -DPATTERN=${pattern} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli.cmake)
  endfunction()

  poker_cli_test(Version 0 "^poker_cli [0-9]+[.][0-9]+[.][0-9]+" --version)
  poker_cli_test(Help 0 "Usage: poker_cli.*QQ[+].*--json" --help)
  poker_cli_test(NoArgumentsPrintsUsage 1 "^usage: poker_cli")
  poker_cli_test(KnownHandsOnTheFlop 0 "AsKs .*QhQd .*990 boards enumerated"
    AsKs QhQd --board Js Ts 2h)
  poker_cli_test(RangesAreEnumerated 0
    "Range .*QQ[+], AKs +22 .*JJ-99 +18 .*320,760 showdowns [(]324 deals"
    "QQ+, AKs" JJ-99 --board Ah 7d 2c)
  poker_cli_test(MonteCarloShowsSeedAndMargin 0
    "95% margin.*[+]/- .*Monte Carlo: 20,000 random boards [(]seed 7[)]"
    AsKs QhQd --trials 20000 --seed 7)
  poker_cli_test(Json 0 "^{.ok.:true,.method.:.exact.,.*,.players.:[[]{.range.:.AsKs.,"
    AsKs QhQd --board Js Ts 2h --json)
  poker_cli_test(JsonError 1 "^{.ok.:false,.field.:.range.,.player.:1," AsKs AKx --json)
  poker_cli_test(InvalidRange 1 "error: invalid range .QQ[+], AKx. at .AKx." AsKs "QQ+, AKx")
  poker_cli_test(InvalidBoard 1 "error: invalid board cards: Js Ts 2x" AsKs QhQd --board Js Ts 2x)
  poker_cli_test(DuplicateCard 1 "error: the same card appears more than once" AsAh AsAh)
  poker_cli_test(TooManyDealsForExact 1 "error: too many deals of the hands to enumerate exactly"
    random random random --exact)
  poker_cli_test(UnknownOption 1 "unknown option: --bogus" AsKs QhQd --bogus)
  poker_cli_test(ExactWithTrials 1 "--exact can't be combined" AsKs QhQd --exact --trials 5)
  poker_cli_test(BadNumber 1 "--trials needs a whole number" AsKs QhQd --trials 1e6)
endif()

# ---- Benchmarks ------------------------------------------------------------
if(POKER_BUILD_BENCH AND POKER_BUILD_PROGRAMS)
  add_executable(poker_bench bench/bench_main.cpp)
  target_link_libraries(poker_bench PRIVATE poker)
  poker_copy_asan_runtime(poker_bench)
endif()
