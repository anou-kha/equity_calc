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

# ---- Core library ----------------------------------------------------------
add_library(poker
  src/card.cpp
)
target_include_directories(poker PUBLIC include)
target_compile_options(poker PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(POKER_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- CLI -------------------------------------------------------------------
add_executable(poker_cli src/main.cpp)
target_link_libraries(poker_cli PRIVATE poker)

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
  )
  target_link_libraries(poker_tests PRIVATE poker GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(poker_tests)
endif()

# ---- Benchmarks ------------------------------------------------------------
if(POKER_BUILD_BENCH)
  add_executable(poker_bench bench/bench_main.cpp)
  target_link_libraries(poker_bench PRIVATE poker)
endif()
