#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "../ds/queue/mutex_deque_mpmc_queue_baseline.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v1.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v2.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v3.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkResult {
  std::string name;
  double elapsed_ms;
  double ns_per_op;
  double mops;
};

template <typename Queue>
Queue make_queue(std::size_t capacity) {
  if constexpr (std::is_constructible_v<Queue, std::size_t>) {
    return Queue(capacity);
  } else {
    (void)capacity;
    return Queue();
  }
}

template <typename Queue>
BenchmarkResult run_queue_bench(std::string name,
                                std::size_t producer_count,
                                std::size_t consumer_count,
                                std::size_t items_per_producer,
                                std::size_t capacity = 0) {
  const std::size_t total_items = producer_count * items_per_producer;
  Queue queue = make_queue<Queue>(capacity);
  std::atomic<std::size_t> done_producers{0};
  std::atomic<std::uint64_t> sum{0};
  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      std::uint64_t value = 0;
      for (;;) {
        if (queue.try_pop(value)) {
          local_sum += value;
          continue;
        }
        if (done_producers.load(std::memory_order_acquire) == producer_count) {
          if (!queue.try_pop(value)) {
            break;
          }
          local_sum += value;
        } else {
          std::this_thread::yield();
        }
      }
      sum.fetch_add(local_sum, std::memory_order_relaxed);
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
        while (!queue.try_push(value)) {
          std::this_thread::yield();
        }
      }
      done_producers.fetch_add(1, std::memory_order_release);
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  for (auto& consumer : consumers) {
    consumer.join();
  }
  queue.close();
  const auto end = Clock::now();

  const std::uint64_t expected_sum =
      (static_cast<std::uint64_t>(total_items) * static_cast<std::uint64_t>(total_items + 1)) / 2;
  if (sum.load(std::memory_order_relaxed) != expected_sum) {
    std::cerr << "unexpected sum for " << name << '\n';
  }

  const auto elapsed_ns =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

  return BenchmarkResult{
      std::move(name),
      elapsed_ns / 1e6,
      elapsed_ns / static_cast<double>(total_items),
      static_cast<double>(total_items) / elapsed_ns * 1e3};
}

template <typename Queue>
BenchmarkResult run_blocking_baseline(std::string name,
                                      std::size_t producer_count,
                                      std::size_t consumer_count,
                                      std::size_t items_per_producer) {
  const std::size_t total_items = producer_count * items_per_producer;
  Queue queue;
  std::atomic<std::uint64_t> sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      std::uint64_t value = 0;
      while (queue.wait_pop(value)) {
        local_sum += value;
      }
      sum.fetch_add(local_sum, std::memory_order_relaxed);
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
  if (sum.load(std::memory_order_relaxed) != expected_sum) {
    std::cerr << "unexpected sum for " << name << '\n';
  }

  const auto elapsed_ns =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  return BenchmarkResult{
      std::move(name),
      elapsed_ns / 1e6,
      elapsed_ns / static_cast<double>(total_items),
      static_cast<double>(total_items) / elapsed_ns * 1e3};
}

void print_result(const BenchmarkResult& result, double baseline_ns_per_op) {
  const double speedup = baseline_ns_per_op / result.ns_per_op;
  std::cout << std::left << std::setw(34) << result.name << " | "
            << std::right << std::setw(9) << std::fixed << std::setprecision(3) << result.elapsed_ms << " ms | "
            << std::setw(9) << std::fixed << std::setprecision(2) << result.ns_per_op << " ns/op | "
            << std::setw(8) << std::fixed << std::setprecision(2) << result.mops << " Mops/s | "
            << std::setw(7) << std::fixed << std::setprecision(2) << speedup << "x\n";
}

}  // namespace

int main() {
  constexpr std::size_t kProducerCount = 4;
  constexpr std::size_t kConsumerCount = 4;
  constexpr std::size_t kItemsPerProducer = 1000;
  constexpr std::size_t kCapacity = 2048;

  const auto baseline = run_blocking_baseline<yutil::queue::MutexDequeMpmcQueueBaseline<std::uint64_t>>(
      "MutexDequeMpmcQueueBaseline(V2)", kProducerCount, kConsumerCount, kItemsPerProducer);
  const auto v1 = run_queue_bench<yutil::queue::RingMutexMpmcQueueV1<std::uint64_t>>(
      "RingMutexMpmcQueueV1", kProducerCount, kConsumerCount, kItemsPerProducer, kCapacity);
  const auto v2 = run_queue_bench<yutil::queue::RingMutexMpmcQueueV2<std::uint64_t>>(
      "RingMutexMpmcQueueV2", kProducerCount, kConsumerCount, kItemsPerProducer, kCapacity);
  const auto v3 = run_queue_bench<yutil::queue::RingMutexMpmcQueueV3<std::uint64_t>>(
      "RingMutexMpmcQueueV3", kProducerCount, kConsumerCount, kItemsPerProducer, kCapacity);

  std::cout << "Ring+Mutex MPMC iteration benchmark (" << kProducerCount << "P/" << kConsumerCount
            << "C, " << (kProducerCount * kItemsPerProducer) << " items)\n";
  std::cout << "Implementation                     |   elapsed |    ns/op |   Mops/s | speedup\n";
  print_result(baseline, baseline.ns_per_op);
  print_result(v1, baseline.ns_per_op);
  print_result(v2, baseline.ns_per_op);
  print_result(v3, baseline.ns_per_op);
  return 0;
}
