#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "../ds/queue/ring_mutex_mpmc_queue_v1.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v2.hpp"
#include "../ds/queue/ring_mutex_mpmc_queue_v3.hpp"

namespace {

template <typename Queue>
void test_basic() {
  Queue queue(8);
  assert(queue.push(1));
  assert(queue.push(2));
  int value = 0;
  assert(queue.try_pop(value));
  assert(value == 1);
  assert(queue.wait_pop(value));
  assert(value == 2);
}

template <typename Queue>
void test_close() {
  Queue queue(8);
  std::atomic<bool> closed_seen{false};
  std::thread consumer([&] {
    int value = 0;
    closed_seen.store(!queue.wait_pop(value), std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  queue.close();
  consumer.join();
  assert(closed_seen.load(std::memory_order_acquire));
  assert(!queue.push(1));
}

template <typename Queue>
void test_mpmc_correctness() {
  constexpr std::size_t kProducerCount = 4;
  constexpr std::size_t kConsumerCount = 4;
  constexpr std::size_t kItemsPerProducer = 20000;
  constexpr std::size_t kTotalItems = kProducerCount * kItemsPerProducer;

  Queue queue(2048);
  std::atomic<std::size_t> consumed_count{0};
  std::atomic<std::uint64_t> consumed_sum{0};

  std::vector<std::thread> consumers;
  consumers.reserve(kConsumerCount);
  for (std::size_t i = 0; i < kConsumerCount; ++i) {
    consumers.emplace_back([&] {
      std::uint64_t local_sum = 0;
      std::size_t local_count = 0;
      std::uint64_t value = 0;
      while (queue.wait_pop(value)) {
        local_sum += value;
        ++local_count;
      }
      consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
      consumed_count.fetch_add(local_count, std::memory_order_relaxed);
    });
  }

  std::vector<std::thread> producers;
  producers.reserve(kProducerCount);
  for (std::size_t p = 0; p < kProducerCount; ++p) {
    producers.emplace_back([&, p] {
      const std::uint64_t base = static_cast<std::uint64_t>(p * kItemsPerProducer);
      for (std::size_t i = 0; i < kItemsPerProducer; ++i) {
        const std::uint64_t value = base + static_cast<std::uint64_t>(i) + 1;
        assert(queue.push(value));
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
      (static_cast<std::uint64_t>(kTotalItems) * static_cast<std::uint64_t>(kTotalItems + 1)) / 2;
  assert(consumed_count.load(std::memory_order_relaxed) == kTotalItems);
  assert(consumed_sum.load(std::memory_order_relaxed) == expected_sum);
}

}  // namespace

int main() {
  test_basic<yutil::queue::RingMutexMpmcQueueV1<int>>();
  test_basic<yutil::queue::RingMutexMpmcQueueV2<int>>();
  test_basic<yutil::queue::RingMutexMpmcQueueV3<int>>();

  test_close<yutil::queue::RingMutexMpmcQueueV1<int>>();
  test_close<yutil::queue::RingMutexMpmcQueueV2<int>>();
  test_close<yutil::queue::RingMutexMpmcQueueV3<int>>();

  test_mpmc_correctness<yutil::queue::RingMutexMpmcQueueV1<std::uint64_t>>();
  test_mpmc_correctness<yutil::queue::RingMutexMpmcQueueV2<std::uint64_t>>();
  test_mpmc_correctness<yutil::queue::RingMutexMpmcQueueV3<std::uint64_t>>();

  std::cout << "ring mutex mpmc queue version tests passed\n";
  return 0;
}
