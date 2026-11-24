#pragma once

// Internal helper for spreading independent tasks over several threads. Used by exact enumeration
// and Monte Carlo; not part of the public API.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

namespace poker::detail {

// A sanity limit, so a typo like "--threads 100000" can't try to start that many threads.
inline constexpr unsigned kMaxThreads = 256;

// How many threads to use: `requested` (0 means one per hardware thread), but never more than
// `maxUseful` (there's no point in more threads than pieces of work), never more than kMaxThreads,
// and at least 1.
inline unsigned threadCount(unsigned requested, uint64_t maxUseful) {
  const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
  const uint64_t wanted = requested != 0 ? requested : hardware;
  return static_cast<unsigned>(
      std::max<uint64_t>(1, std::min({wanted, maxUseful, uint64_t{kMaxThreads}})));
}

// Runs task(i, state) for every task number i in [0, numTasks), on `threads` threads including the
// calling one, and returns the State of every thread that took part, for the caller to combine.
//
// How the work is shared: a single atomic counter hands out task numbers. Whenever a thread
// finishes a task it claims the next unclaimed one, so faster threads simply do more tasks and
// nobody waits for a slow thread's fixed share.
//
// Why a State per thread: each thread adds its results into its own local State and copies it out
// only at the very end. Threads never write to memory another thread is using, so they need no
// locks, and they don't slow each other down by fighting over the same cache line ("false
// sharing"), which would happen if they all updated counters sitting next to each other.
//
// If the operating system can't start a thread, the threads that did start share its tasks: the
// atomic counter makes sure every task is still done exactly once. `task` must not throw.
template <typename State, typename Task>
std::vector<State> runInParallel(uint64_t numTasks, unsigned threads, const Task& task) {
  std::atomic<uint64_t> nextTask = 0;
  std::vector<State> states(threads);

  const auto work = [&](unsigned threadIndex) {
    State local{};
    // Relaxed ordering is enough: the counter only has to give each task to exactly one thread.
    // The results travel back through `states`, and join() below makes them visible.
    for (uint64_t i = nextTask.fetch_add(1, std::memory_order_relaxed); i < numTasks;
         i = nextTask.fetch_add(1, std::memory_order_relaxed)) {
      task(i, local);
    }
    states[threadIndex] = local;
  };

  std::vector<std::thread> helpers;
  helpers.reserve(threads - 1);
  for (unsigned t = 1; t < threads; ++t) {
    try {
      helpers.emplace_back(work, t);
    } catch (const std::exception&) {
      break;  // couldn't start another thread: the ones already running share its tasks
    }
  }
  work(0);  // the calling thread does its share too
  for (std::thread& helper : helpers) helper.join();

  states.resize(helpers.size() + 1);  // drop the slots of any threads that never started
  return states;
}

}  // namespace poker::detail
