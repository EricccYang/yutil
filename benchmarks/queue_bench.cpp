#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include "../../concurrentqueue/blockingconcurrentqueue.h"
#include "../../SPSCQueue/include/rigtorp/SPSCQueue.h"

#include "../ds/mutex_blocking_queue.hpp"
#include "../ds/spsc_bounded_queue.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
  std::string name;
  std::size_t operations;
  double elapsed_ms;
  double ns_per_op;
  double mops;
};

template <typename Producer, typename Consumer>
BenchmarkResult run_pair_benchmark(std::string name,
                                   std::size_t operations,
                                   Producer producer,
                                   Consumer consumer) {
  std::thread consumer_thread([&] { consumer(); });

  const auto start = Clock::now();
  producer();
  consumer_thread.join();
  const auto end = Clock::now();

  const auto elapsed_ns =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  return BenchmarkResult{
      std::move(name),
      operations,
      elapsed_ns / 1e6,
      elapsed_ns / static_cast<double>(operations),
      static_cast<double>(operations) / elapsed_ns * 1e3};
}

BenchmarkResult bench_mutex_queue(std::size_t operations) {
  yutil::MutexBlockingQueue<int> queue;
  std::uint64_t sum = 0;

  auto result = run_pair_benchmark(
      "yutil::MutexBlockingQueue",
      operations,
      [&] {
        for (std::size_t i = 0; i < operations; ++i) {
          queue.push(static_cast<int>(i));
        }
        queue.close();
      },
      [&] {
        int value = 0;
        while (queue.wait_pop(value)) {
          sum += static_cast<std::uint64_t>(value);
        }
      });

  if (sum == 0 && operations > 1) {
    std::cerr << "unexpected sum for mutex queue\n";
  }
  return result;
}

BenchmarkResult bench_yutil_spsc(std::size_t operations) {
  yutil::SpscBoundedQueue<int> queue(1u << 12U);
  std::uint64_t sum = 0;

  auto result = run_pair_benchmark(
      "yutil::SpscBoundedQueue",
      operations,
      [&] {
        for (std::size_t i = 0; i < operations; ++i) {
          while (!queue.try_push(static_cast<int>(i))) {
          }
        }
      },
      [&] {
        int value = 0;
        for (std::size_t i = 0; i < operations; ++i) {
          while (!queue.try_pop(value)) {
          }
          sum += static_cast<std::uint64_t>(value);
        }
      });

  if (sum == 0 && operations > 1) {
    std::cerr << "unexpected sum for yutil spsc queue\n";
  }
  return result;
}

BenchmarkResult bench_rigtorp_spsc(std::size_t operations) {
  rigtorp::SPSCQueue<int> queue(1u << 12U);
  std::uint64_t sum = 0;

  auto result = run_pair_benchmark(
      "rigtorp::SPSCQueue",
      operations,
      [&] {
        for (std::size_t i = 0; i < operations; ++i) {
          while (!queue.try_push(static_cast<int>(i))) {
          }
        }
      },
      [&] {
        int value = 0;
        for (std::size_t i = 0; i < operations; ++i) {
          while (!queue.front()) {
          }
          value = *queue.front();
          queue.pop();
          sum += static_cast<std::uint64_t>(value);
        }
      });

  if (sum == 0 && operations > 1) {
    std::cerr << "unexpected sum for rigtorp spsc queue\n";
  }
  return result;
}

BenchmarkResult bench_blocking_concurrent_queue(std::size_t operations) {
  moodycamel::BlockingConcurrentQueue<int> queue(1u << 12U);
  std::uint64_t sum = 0;

  auto result = run_pair_benchmark(
      "moodycamel::BlockingConcurrentQueue",
      operations,
      [&] {
        for (std::size_t i = 0; i < operations; ++i) {
          queue.enqueue(static_cast<int>(i));
        }
      },
      [&] {
        int value = 0;
        for (std::size_t i = 0; i < operations; ++i) {
          queue.wait_dequeue(value);
          sum += static_cast<std::uint64_t>(value);
        }
      });

  if (sum == 0 && operations > 1) {
    std::cerr << "unexpected sum for blocking concurrent queue\n";
  }
  return result;
}

void print_result(const BenchmarkResult& result) {
  std::cout << std::left << std::setw(36) << result.name << " | "
            << std::right << std::setw(10) << std::fixed << std::setprecision(3) << result.elapsed_ms << " ms | "
            << std::setw(10) << std::fixed << std::setprecision(2) << result.ns_per_op << " ns/op | "
            << std::setw(8) << std::fixed << std::setprecision(2) << result.mops << " Mops/s\n";
}

}  // namespace

int main() {
  constexpr std::size_t kOperations = 1'000'000;

  std::cout << "Queue throughput benchmark (" << kOperations << " items, 1 producer / 1 consumer)\n";
  std::cout << "Implementation                         |   elapsed |     ns/op |   Mops/s\n";

  print_result(bench_mutex_queue(kOperations));
  print_result(bench_yutil_spsc(kOperations));
  print_result(bench_rigtorp_spsc(kOperations));
  print_result(bench_blocking_concurrent_queue(kOperations));

  return 0;
}
