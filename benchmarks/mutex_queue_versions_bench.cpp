#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include "../ds/queue/mutex_deque_queue_v1.hpp"
#include "../ds/queue/mutex_deque_queue_v2.hpp"
#include "../ds/queue/mutex_deque_queue_v3.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
  std::string name;
  double elapsed_ms;
  double ns_per_op;
  double mops;
};

template <typename Queue>
BenchmarkResult bench_queue(std::string name, std::size_t operations) {
  Queue queue;
  std::uint64_t sum = 0;

  std::thread consumer([&] {
    int value = 0;
    while (queue.wait_pop(value)) {
      sum += static_cast<std::uint64_t>(value);
    }
  });

  const auto start = Clock::now();
  for (std::size_t i = 0; i < operations; ++i) {
    queue.push(static_cast<int>(i));
  }
  queue.close();
  consumer.join();
  const auto end = Clock::now();

  if (sum == 0 && operations > 1) {
    std::cerr << "unexpected sum for " << name << '\n';
  }

  const auto elapsed_ns =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

  return BenchmarkResult{
      std::move(name),
      elapsed_ns / 1e6,
      elapsed_ns / static_cast<double>(operations),
      static_cast<double>(operations) / elapsed_ns * 1e3};
}

void print_result(const BenchmarkResult& result, double baseline_ns_per_op) {
  const double speedup = baseline_ns_per_op / result.ns_per_op;
  std::cout << std::left << std::setw(28) << result.name << " | "
            << std::right << std::setw(9) << std::fixed << std::setprecision(3) << result.elapsed_ms << " ms | "
            << std::setw(9) << std::fixed << std::setprecision(2) << result.ns_per_op << " ns/op | "
            << std::setw(8) << std::fixed << std::setprecision(2) << result.mops << " Mops/s | "
            << std::setw(7) << std::fixed << std::setprecision(2) << speedup << "x\n";
}

}  // namespace

int main() {
  constexpr std::size_t kOperations = 1'000'000;

  const auto v1 = bench_queue<yutil::queue::MutexDequeQueueV1<int>>("MutexDequeQueueV1", kOperations);
  const auto v2 = bench_queue<yutil::queue::MutexDequeQueueV2<int>>("MutexDequeQueueV2", kOperations);
  const auto v3 = bench_queue<yutil::queue::MutexDequeQueueV3<int>>("MutexDequeQueueV3", kOperations);

  std::cout << "Mutex deque queue iteration benchmark (" << kOperations << " items, 1 producer / 1 consumer)\n";
  std::cout << "Implementation               |   elapsed |    ns/op |   Mops/s | speedup\n";
  print_result(v1, v1.ns_per_op);
  print_result(v2, v1.ns_per_op);
  print_result(v3, v1.ns_per_op);

  return 0;
}
