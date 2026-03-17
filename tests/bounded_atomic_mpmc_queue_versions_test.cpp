#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "../ds/queue/bounded_atomic_mpmc_queue_v1.hpp"
#include "../ds/queue/bounded_atomic_mpmc_queue_v2.hpp"
#include "../ds/queue/bounded_atomic_mpmc_queue_v3.hpp"

namespace {

template <typename Queue>
void test_single_thread_basic() {
  Queue queue(128);
  assert(queue.try_push(1));
  assert(queue.try_push(2));
  int value = 0;
  assert(queue.try_pop(value));
  assert(value == 1);
  assert(queue.try_pop(value));
  assert(value == 2);
  queue.close();
  assert(!queue.try_push(3));
}

template <typename Queue>
void test_mpmc_correctness() {
  constexpr std::size_t kProducerCount = 4;
  constexpr std::size_t kConsumerCount = 4;
  constexpr std::size_t kItemsPerProducer = 30000;
  constexpr std::size_t kTotalItems = kProducerCount * kItemsPerProducer;

  Queue queue(4096);
  std::atomic<std::size_t> done_producers{0};
  std::atomic<std::size_t> consumed_count{0};
  std::atomic<std::uint64_t> consumed_sum{0};

  std::vector<std::thread> producers;
  producers.reserve(kProducerCount);
  for (std::size_t p = 0; p < kProducerCount; ++p) {
    producers.emplace_back([&, p] {
      const std::uint64_t base = static_cast<std::uint64_t>(p * kItemsPerProducer);
      for (std::size_t i = 0; i < kItemsPerProducer; ++i) {
        const std::uint64_t value = base + static_cast<std::uint64_t>(i) + 1;
        while (!queue.try_push(value)) {
          std::this_thread::yield();
        }
      }
      done_producers.fetch_add(1, std::memory_order_release);
    });
  }

  std::vector<std::thread> consumers;
  consumers.reserve(kConsumerCount);
  for (std::size_t c = 0; c < kConsumerCount; ++c) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      std::size_t local_count = 0;
      std::uint64_t value = 0;
      for (;;) {
        if (queue.try_pop(value)) {
          local_sum += value;
          ++local_count;
          continue;
        }
        if (done_producers.load(std::memory_order_acquire) == kProducerCount) {
          if (!queue.try_pop(value)) {
            break;
          }
          local_sum += value;
          ++local_count;
        } else {
          std::this_thread::yield();
        }
      }
      consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
      consumed_count.fetch_add(local_count, std::memory_order_relaxed);
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
      (static_cast<std::uint64_t>(kTotalItems) * static_cast<std::uint64_t>(kTotalItems + 1)) / 2;
  assert(consumed_count.load(std::memory_order_relaxed) == kTotalItems);
  assert(consumed_sum.load(std::memory_order_relaxed) == expected_sum);
}

}  // namespace

int main() {
  test_single_thread_basic<yutil::queue::BoundedAtomicMpmcQueueV1<int>>();
  test_single_thread_basic<yutil::queue::BoundedAtomicMpmcQueueV2<int>>();
  test_single_thread_basic<yutil::queue::BoundedAtomicMpmcQueueV3<int>>();

  test_mpmc_correctness<yutil::queue::BoundedAtomicMpmcQueueV1<std::uint64_t>>();
  test_mpmc_correctness<yutil::queue::BoundedAtomicMpmcQueueV2<std::uint64_t>>();
  test_mpmc_correctness<yutil::queue::BoundedAtomicMpmcQueueV3<std::uint64_t>>();

  std::cout << "bounded atomic mpmc queue version tests passed\n";
  return 0;
}
