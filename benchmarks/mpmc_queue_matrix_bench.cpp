#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../ds/queue/bounded_atomic_mpmc_queue_v2.hpp"
#include "../ds/queue/mutex_deque_mpmc_queue_baseline.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v2.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct HeavyPayload {
  std::uint64_t id{0};
  std::string symbol;
  std::vector<std::uint64_t> levels;

  HeavyPayload() = default;
  explicit HeavyPayload(std::uint64_t seq)
      : id(seq), symbol("PX" + std::to_string(seq % 64)), levels(24, seq) {}
};

struct BenchLine {
  std::string payload;
  std::string impl;
  std::size_t producers;
  std::size_t consumers;
  std::size_t total_items;
  double median_ns_per_op;
  double mops;
};

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

template <typename QueueFactory, typename PayloadFactory, typename IdExtractor>
double run_blocking_once(QueueFactory make_queue,
                         PayloadFactory make_payload,
                         IdExtractor extract_id,
                         std::size_t producer_count,
                         std::size_t consumer_count,
                         std::size_t items_per_producer) {
  auto queue = make_queue();
  const std::size_t total_items = producer_count * items_per_producer;
  std::atomic<std::uint64_t> sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      std::size_t local_count = 0;
      typename std::remove_reference<decltype(make_payload(0))>::type value;
      while (queue.wait_pop(value)) {
        local_sum += extract_id(value);
        ++local_count;
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
        auto payload = make_payload(base + static_cast<std::uint64_t>(i) + 1);
        queue.push(std::move(payload));
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
    std::cerr << "sum mismatch in blocking benchmark\n";
  }

  return static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

template <typename QueueFactory, typename PayloadFactory, typename IdExtractor>
double run_try_once(QueueFactory make_queue,
                    PayloadFactory make_payload,
                    IdExtractor extract_id,
                    std::size_t producer_count,
                    std::size_t consumer_count,
                    std::size_t items_per_producer) {
  auto queue = make_queue();
  const std::size_t total_items = producer_count * items_per_producer;
  std::atomic<std::size_t> done_producers{0};
  std::atomic<std::uint64_t> sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      typename std::remove_reference<decltype(make_payload(0))>::type value;
      for (;;) {
        if (queue.try_pop(value)) {
          local_sum += extract_id(value);
          continue;
        }
        if (done_producers.load(std::memory_order_acquire) == producer_count) {
          if (!queue.try_pop(value)) {
            break;
          }
          local_sum += extract_id(value);
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
        auto payload = make_payload(base + static_cast<std::uint64_t>(i) + 1);
        while (!queue.try_push(std::move(payload))) {
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
    std::cerr << "sum mismatch in try benchmark\n";
  }

  return static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

template <typename PayloadFactory, typename IdExtractor>
void run_payload_matrix(const std::string& payload_name,
                        std::size_t items_per_producer,
                        PayloadFactory make_payload,
                        IdExtractor extract_id,
                        std::vector<BenchLine>& lines) {
  const std::vector<std::pair<std::size_t, std::size_t>> scenarios = {
      {1, 1}, {2, 2}, {4, 4}, {8, 8}};
  constexpr std::size_t kRuns = 3;
  constexpr std::size_t kCapacity = 4096;

  for (const auto& scenario : scenarios) {
    const std::size_t producers = scenario.first;
    const std::size_t consumers = scenario.second;
    const std::size_t total_items = producers * items_per_producer;

    std::vector<double> baseline_samples;
    std::vector<double> ring_samples;
    std::vector<double> atomic_samples;
    baseline_samples.reserve(kRuns);
    ring_samples.reserve(kRuns);
    atomic_samples.reserve(kRuns);

    for (std::size_t run = 0; run < kRuns; ++run) {
      baseline_samples.push_back(run_blocking_once(
          [] { return yutil::queue::MutexDequeMpmcQueueBaseline<decltype(make_payload(0))>(); },
          make_payload, extract_id, producers, consumers, items_per_producer));
      ring_samples.push_back(run_blocking_once(
          [kCapacity] { return yutil::queue::RingMutexMpmcQueueV2<decltype(make_payload(0))>(kCapacity); },
          make_payload, extract_id, producers, consumers, items_per_producer));
      atomic_samples.push_back(run_try_once(
          [kCapacity] { return yutil::queue::BoundedAtomicMpmcQueueV2<decltype(make_payload(0))>(kCapacity); },
          make_payload, extract_id, producers, consumers, items_per_producer));
    }

    const auto baseline_ns = median(baseline_samples);
    const auto ring_ns = median(ring_samples);
    const auto atomic_ns = median(atomic_samples);

    lines.push_back({payload_name,
                     "MutexDequeMpmcQueueBaseline(V2)",
                     producers,
                     consumers,
                     total_items,
                     baseline_ns / static_cast<double>(total_items),
                     static_cast<double>(total_items) / baseline_ns * 1e3});
    lines.push_back({payload_name,
                     "RingMutexMpmcQueueV2",
                     producers,
                     consumers,
                     total_items,
                     ring_ns / static_cast<double>(total_items),
                     static_cast<double>(total_items) / ring_ns * 1e3});
    lines.push_back({payload_name,
                     "BoundedAtomicMpmcQueueV2",
                     producers,
                     consumers,
                     total_items,
                     atomic_ns / static_cast<double>(total_items),
                     static_cast<double>(total_items) / atomic_ns * 1e3});
  }
}

void print_lines(const std::vector<BenchLine>& lines) {
  std::cout << "MPMC payload matrix benchmark (median of 3 runs, Release)\n";
  std::cout << "payload      | impl                              | P/C  |  items   |   ns/op |  Mops/s\n";
  for (const auto& line : lines) {
    std::cout << std::left << std::setw(12) << line.payload << " | "
              << std::setw(33) << line.impl << " | "
              << std::right << std::setw(1) << line.producers << "/" << std::setw(1) << line.consumers << " | "
              << std::setw(8) << line.total_items << " | "
              << std::setw(7) << std::fixed << std::setprecision(2) << line.median_ns_per_op << " | "
              << std::setw(7) << std::fixed << std::setprecision(2) << line.mops << "\n";
  }
}

}  // namespace

int main() {
  std::vector<BenchLine> lines;
  lines.reserve(3 * 4 * 3);

  run_payload_matrix(
      "u64",
      50000,
      [](std::uint64_t seq) { return seq; },
      [](const std::uint64_t& value) { return value; },
      lines);

  run_payload_matrix(
      "unique_ptr",
      20000,
      [](std::uint64_t seq) { return std::make_unique<std::uint64_t>(seq); },
      [](const std::unique_ptr<std::uint64_t>& value) { return *value; },
      lines);

  run_payload_matrix(
      "heavy",
      8000,
      [](std::uint64_t seq) { return HeavyPayload(seq); },
      [](const HeavyPayload& value) { return value.id; },
      lines);

  print_lines(lines);
  return 0;
}
