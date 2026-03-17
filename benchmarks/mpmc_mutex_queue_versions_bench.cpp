#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../ds/queue/mutex_deque_mpmc_queue_v1.hpp"
#include "../ds/queue/mutex_deque_mpmc_queue_v2.hpp"
#include "../ds/queue/mutex_deque_mpmc_queue_v3.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
  std::string name;
  std::size_t operations;
  double elapsed_ms;
  double ns_per_op;
  double mops;
};

template <typename Queue>
BenchmarkResult bench_queue(std::string name,
                            std::size_t producer_count,
                            std::size_t consumer_count,
                            std::size_t items_per_producer) {
  const std::size_t total_items = producer_count * items_per_producer;
  Queue queue;

  std::uint64_t sum = 0;
  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      std::uint64_t value = 0;
      while (queue.wait_pop(value)) {
        local_sum += value;
      }
      // Single merge point after work completion to reduce atomic overhead.
      static std::mutex sum_mutex;
      std::lock_guard<std::mutex> lock(sum_mutex);
      sum += local_sum;
    });
  }

  const auto start = Clock::now();
  std::vector<std::thread> producers;
  producers.reserve(producer_count);
  for (std::size_t p = 0; p < producer_count; ++p) {
    producers.emplace_back([&, p] {
      const std::uint64_t base = static_cast<std::uint64_t>(p * items_per_producer);
      for (std::size_t i = 0; i < items_per_producer; ++i) {
        const std::uint64_t value = base + static_cast<std::uint64_t>(i) + 1;
        queue.push(value);
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  queue.close();
  for (auto& consumer : consumers) {
    consumer.join();
  }
  const auto end = Clock::now();

  const std::uint64_t expected_sum =
      (static_cast<std::uint64_t>(total_items) * static_cast<std::uint64_t>(total_items + 1)) / 2;
  if (sum != expected_sum) {
    std::cerr << "unexpected sum for " << name << " expected=" << expected_sum << " got=" << sum << '\n';
  }

  const auto elapsed_ns =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  return BenchmarkResult{
      std::move(name),
      total_items,
      elapsed_ns / 1e6,
      elapsed_ns / static_cast<double>(total_items),
      static_cast<double>(total_items) / elapsed_ns * 1e3};
}

void print_result(const BenchmarkResult& result, double baseline_ns_per_op) {
  const double speedup = baseline_ns_per_op / result.ns_per_op;
  std::cout << std::left << std::setw(32) << result.name << " | "
            << std::right << std::setw(9) << std::fixed << std::setprecision(3) << result.elapsed_ms << " ms | "
            << std::setw(9) << std::fixed << std::setprecision(2) << result.ns_per_op << " ns/op | "
            << std::setw(8) << std::fixed << std::setprecision(2) << result.mops << " Mops/s | "
            << std::setw(7) << std::fixed << std::setprecision(2) << speedup << "x\n";
}

}  // namespace

int main() {
  constexpr std::size_t kProducerCount = 4;
  constexpr std::size_t kConsumerCount = 4;
  constexpr std::size_t kItemsPerProducer = 250000;

  const auto v1 =
      bench_queue<yutil::queue::MutexDequeMpmcQueueV1<std::uint64_t>>("MutexDequeMpmcQueueV1", kProducerCount, kConsumerCount, kItemsPerProducer);
  const auto v2 =
      bench_queue<yutil::queue::MutexDequeMpmcQueueV2<std::uint64_t>>("MutexDequeMpmcQueueV2", kProducerCount, kConsumerCount, kItemsPerProducer);
  const auto v3 =
      bench_queue<yutil::queue::MutexDequeMpmcQueueV3<std::uint64_t>>("MutexDequeMpmcQueueV3", kProducerCount, kConsumerCount, kItemsPerProducer);

  std::cout << "Mutex deque MPMC iteration benchmark"
            << " (" << kProducerCount << "P/" << kConsumerCount << "C, "
            << (kProducerCount * kItemsPerProducer) << " items)\n";
  std::cout << "Implementation                   |   elapsed |    ns/op |   Mops/s | speedup\n";
  print_result(v1, v1.ns_per_op);
  print_result(v2, v1.ns_per_op);
  print_result(v3, v1.ns_per_op);

  return 0;
}
