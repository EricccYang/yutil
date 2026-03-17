#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#include "../ds/mutex_blocking_queue.hpp"
#include "../ds/spsc_bounded_queue.hpp"

namespace {

void test_mutex_blocking_queue_basic() {
  yutil::MutexBlockingQueue<int> queue;
  assert(queue.push(1));
  assert(queue.push(2));

  int value = 0;
  assert(queue.try_pop(value));
  assert(value == 1);
  assert(queue.wait_pop(value));
  assert(value == 2);
  assert(!queue.try_pop(value));
}

void test_mutex_blocking_queue_close() {
  yutil::MutexBlockingQueue<int> queue;
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

void test_spsc_bounded_queue_basic() {
  yutil::SpscBoundedQueue<int> queue(8);
  assert(queue.capacity() >= 8);
  assert(queue.try_push(1));
  assert(queue.try_push(2));

  int value = 0;
  assert(queue.try_pop(value));
  assert(value == 1);
  assert(queue.try_pop(value));
  assert(value == 2);
  assert(!queue.try_pop(value));
}

void test_spsc_bounded_queue_threaded() {
  constexpr int kCount = 100000;
  yutil::SpscBoundedQueue<int> queue(1024);
  std::vector<int> values;
  values.reserve(kCount);

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!queue.try_push(i)) {
      }
    }
  });

  std::thread consumer([&] {
    int value = 0;
    for (int i = 0; i < kCount; ++i) {
      while (!queue.try_pop(value)) {
      }
      values.push_back(value);
    }
  });

  producer.join();
  consumer.join();

  assert(static_cast<int>(values.size()) == kCount);
  for (int i = 0; i < kCount; ++i) {
    assert(values[static_cast<std::size_t>(i)] == i);
  }
}

}  // namespace

int main() {
  test_mutex_blocking_queue_basic();
  test_mutex_blocking_queue_close();
  test_spsc_bounded_queue_basic();
  test_spsc_bounded_queue_threaded();

  std::cout << "queue tests passed\n";
  return 0;
}
