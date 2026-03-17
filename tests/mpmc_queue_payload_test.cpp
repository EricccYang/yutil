#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <iostream>

#include "../ds/queue/bounded_atomic_mpmc_queue_v2.hpp"
#include "../ds/queue/mutex_deque_mpmc_queue_baseline.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v2.hpp"

namespace {

struct HeavyPayload {
  std::uint64_t id{0};
  std::string symbol;
  std::vector<std::uint64_t> book_levels;

  HeavyPayload() = default;
  explicit HeavyPayload(std::uint64_t seq)
      : id(seq), symbol("SYM" + std::to_string(seq % 32)), book_levels(16, seq) {}
};

template <typename Queue, typename PayloadFactory>
void run_blocking_payload_test(Queue& queue,
                               std::size_t producer_count,
                               std::size_t consumer_count,
                               std::size_t items_per_producer,
                               PayloadFactory make_payload) {
  const std::size_t total_items = producer_count * items_per_producer;
  std::atomic<std::size_t> consumed_count{0};
  std::atomic<std::uint64_t> consumed_sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::size_t local_count = 0;
      std::uint64_t local_sum = 0;
      typename std::remove_reference<decltype(make_payload(0))>::type value;
      while (queue.wait_pop(value)) {
        if constexpr (std::is_same_v<decltype(value), std::unique_ptr<std::uint64_t>>) {
          local_sum += *value;
        } else {
          local_sum += value.id;
        }
        ++local_count;
      }
      consumed_count.fetch_add(local_count, std::memory_order_relaxed);
      consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
    });
  }

  std::vector<std::thread> producers;
  producers.reserve(producer_count);
  for (std::size_t p = 0; p < producer_count; ++p) {
    producers.emplace_back([&, p] {
      const std::uint64_t base = static_cast<std::uint64_t>(p * items_per_producer);
      for (std::size_t i = 0; i < items_per_producer; ++i) {
        auto payload = make_payload(base + static_cast<std::uint64_t>(i) + 1);
        assert(queue.push(std::move(payload)));
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

  const std::uint64_t expected_sum =
      (static_cast<std::uint64_t>(total_items) * static_cast<std::uint64_t>(total_items + 1)) / 2;
  assert(consumed_count.load(std::memory_order_relaxed) == total_items);
  assert(consumed_sum.load(std::memory_order_relaxed) == expected_sum);
}

template <typename Queue, typename PayloadFactory>
void run_try_payload_test(Queue& queue,
                          std::size_t producer_count,
                          std::size_t consumer_count,
                          std::size_t items_per_producer,
                          PayloadFactory make_payload) {
  const std::size_t total_items = producer_count * items_per_producer;
  std::atomic<std::size_t> done_producers{0};
  std::atomic<std::size_t> consumed_count{0};
  std::atomic<std::uint64_t> consumed_sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(consumer_count);
  for (std::size_t c = 0; c < consumer_count; ++c) {
    consumers.emplace_back([&] {
      std::size_t local_count = 0;
      std::uint64_t local_sum = 0;
      typename std::remove_reference<decltype(make_payload(0))>::type value;
      for (;;) {
        if (queue.try_pop(value)) {
          if constexpr (std::is_same_v<decltype(value), std::unique_ptr<std::uint64_t>>) {
            local_sum += *value;
          } else {
            local_sum += value.id;
          }
          ++local_count;
          continue;
        }
        if (done_producers.load(std::memory_order_acquire) == producer_count) {
          if (!queue.try_pop(value)) {
            break;
          }
          if constexpr (std::is_same_v<decltype(value), std::unique_ptr<std::uint64_t>>) {
            local_sum += *value;
          } else {
            local_sum += value.id;
          }
          ++local_count;
        } else {
          std::this_thread::yield();
        }
      }
      consumed_count.fetch_add(local_count, std::memory_order_relaxed);
      consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
    });
  }

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

  const std::uint64_t expected_sum =
      (static_cast<std::uint64_t>(total_items) * static_cast<std::uint64_t>(total_items + 1)) / 2;
  assert(consumed_count.load(std::memory_order_relaxed) == total_items);
  assert(consumed_sum.load(std::memory_order_relaxed) == expected_sum);
}

void test_move_only_payload() {
  constexpr std::size_t kProducerCount = 3;
  constexpr std::size_t kConsumerCount = 3;
  constexpr std::size_t kItemsPerProducer = 15000;

  {
    yutil::queue::MutexDequeMpmcQueueBaseline<std::unique_ptr<std::uint64_t>> queue;
    run_blocking_payload_test(queue, kProducerCount, kConsumerCount, kItemsPerProducer,
                              [](std::uint64_t seq) { return std::make_unique<std::uint64_t>(seq); });
  }
  {
    yutil::queue::RingMutexMpmcQueueV2<std::unique_ptr<std::uint64_t>> queue(2048);
    run_blocking_payload_test(queue, kProducerCount, kConsumerCount, kItemsPerProducer,
                              [](std::uint64_t seq) { return std::make_unique<std::uint64_t>(seq); });
  }
  {
    yutil::queue::BoundedAtomicMpmcQueueV2<std::unique_ptr<std::uint64_t>> queue(2048);
    run_try_payload_test(queue, kProducerCount, kConsumerCount, kItemsPerProducer,
                         [](std::uint64_t seq) { return std::make_unique<std::uint64_t>(seq); });
  }
}

void test_heavy_payload() {
  constexpr std::size_t kProducerCount = 3;
  constexpr std::size_t kConsumerCount = 3;
  constexpr std::size_t kItemsPerProducer = 8000;

  {
    yutil::queue::MutexDequeMpmcQueueBaseline<HeavyPayload> queue;
    run_blocking_payload_test(queue, kProducerCount, kConsumerCount, kItemsPerProducer,
                              [](std::uint64_t seq) { return HeavyPayload(seq); });
  }
  {
    yutil::queue::RingMutexMpmcQueueV2<HeavyPayload> queue(2048);
    run_blocking_payload_test(queue, kProducerCount, kConsumerCount, kItemsPerProducer,
                              [](std::uint64_t seq) { return HeavyPayload(seq); });
  }
  {
    yutil::queue::BoundedAtomicMpmcQueueV2<HeavyPayload> queue(2048);
    run_try_payload_test(queue, kProducerCount, kConsumerCount, kItemsPerProducer,
                         [](std::uint64_t seq) { return HeavyPayload(seq); });
  }
}

}  // namespace

int main() {
  test_move_only_payload();
  test_heavy_payload();
  std::cout << "mpmc payload tests passed\n";
  return 0;
}
