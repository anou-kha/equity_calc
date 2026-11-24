cmake_minimum_required(VERSION 3.20)
project(poker_equity VERSION 0.1.0 LANGUAGES CXX)

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
  src/card.cpp
  src/equity.cpp
  src/evaluator.cpp
  src/fast_evaluator.cpp
  src/monte_carlo.cpp
  src/random.cpp
  src/showdown.cpp
)
target_include_directories(poker PUBLIC include)
find_package(Threads REQUIRED)  # exact and Monte Carlo equity can use several std::threads
target_link_libraries(poker PUBLIC Threads::Threads)
target_compile_options(poker PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ---- CLI -------------------------------------------------------------------
add_executable(poker_cli src/main.cpp)
target_link_libraries(poker_cli PRIVATE poker)
poker_copy_asan_runtime(poker_cli)

# ---- Tests -----------------------------------------------------------------
if(POKER_BUILD_TESTS)
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
    tests/test_card.cpp
    tests/test_equity.cpp
    tests/test_evaluator.cpp
    tests/test_fast_evaluator.cpp
    tests/test_monte_carlo.cpp
    tests/test_multithreading.cpp
    tests/test_random.cpp
  )
  target_link_libraries(poker_tests PRIVATE poker GTest::gtest_main)
  # Must run before gtest_discover_tests' own post-build step, which launches the executable.
  poker_copy_asan_runtime(poker_tests)
  include(GoogleTest)
  gtest_discover_tests(poker_tests)
endif()

# ---- Benchmarks ------------------------------------------------------------
if(POKER_BUILD_BENCH)
  add_executable(poker_bench bench/bench_main.cpp)
  target_link_libraries(poker_bench PRIVATE poker)
  poker_copy_asan_runtime(poker_bench)
endif()
