#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace yutil::queue {

template <typename T>
class MutexDequeMpmcQueueV3 {
 public:
  MutexDequeMpmcQueueV3() = default;

  MutexDequeMpmcQueueV3(const MutexDequeMpmcQueueV3&) = delete;
  MutexDequeMpmcQueueV3& operator=(const MutexDequeMpmcQueueV3&) = delete;

  bool push(const T& value) { return emplace(value); }
  bool push(T&& value) { return emplace(std::move(value)); }

  template <typename... Args>
  bool emplace(Args&&... args) {
    bool should_notify = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return false;
      }
      queue_.emplace_back(std::forward<Args>(args)...);
      // V3: notify only if there is an actual blocked consumer.
      should_notify = waiting_consumers_ > 0;
    }

    if (should_notify) {
      cv_.notify_one();
    }
    return true;
  }

  bool wait_pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (queue_.empty() && !closed_) {
      ++waiting_consumers_;
      cv_.wait(lock);
      --waiting_consumers_;
    }
    if (queue_.empty()) {
      return false;
    }

    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  bool try_pop(T& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<T> queue_;
  std::size_t waiting_consumers_{0};
  bool closed_{false};
};

}  // namespace yutil::queue
