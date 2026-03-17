#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "../ds/queue/mutex_deque_queue_v1.hpp"
#include "../ds/queue/mutex_deque_queue_v2.hpp"
#include "../ds/queue/mutex_deque_queue_v3.hpp"

namespace {

template <typename Queue>
void test_basic_fifo() {
  Queue queue;
  assert(queue.push(1));
  assert(queue.push(2));

  int value = 0;
  assert(queue.try_pop(value));
  assert(value == 1);
  assert(queue.wait_pop(value));
  assert(value == 2);
  assert(!queue.try_pop(value));
}

template <typename Queue>
void test_close_behavior() {
  Queue queue;
  std::atomic<bool> observed_close{false};

  std::thread consumer([&] {
    int value = 0;
    observed_close.store(!queue.wait_pop(value), std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  queue.close();
  consumer.join();

  assert(observed_close.load(std::memory_order_acquire));
  assert(!queue.push(42));
}

template <typename Queue>
void test_threaded_order() {
  constexpr int kCount = 100000;
  Queue queue;
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      assert(queue.push(i));
      produced.store(i + 1, std::memory_order_release);
    }
    queue.close();
  });

  std::thread consumer([&] {
    int expected = 0;
    int value = 0;
    while (queue.wait_pop(value)) {
      assert(value == expected);
      ++expected;
    }
    consumed.store(expected, std::memory_order_release);
  });

  producer.join();
  consumer.join();

  assert(produced.load(std::memory_order_acquire) == kCount);
  assert(consumed.load(std::memory_order_acquire) == kCount);
}

}  // namespace

int main() {
  test_basic_fifo<yutil::queue::MutexDequeQueueV1<int>>();
  test_basic_fifo<yutil::queue::MutexDequeQueueV2<int>>();
  test_basic_fifo<yutil::queue::MutexDequeQueueV3<int>>();

  test_close_behavior<yutil::queue::MutexDequeQueueV1<int>>();
  test_close_behavior<yutil::queue::MutexDequeQueueV2<int>>();
  test_close_behavior<yutil::queue::MutexDequeQueueV3<int>>();

  test_threaded_order<yutil::queue::MutexDequeQueueV1<int>>();
  test_threaded_order<yutil::queue::MutexDequeQueueV2<int>>();
  test_threaded_order<yutil::queue::MutexDequeQueueV3<int>>();

  std::cout << "mutex queue version tests passed\n";
  return 0;
}
