#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace yutil::queue {

template <typename T>
class MutexDequeQueueV3 {
 public:
  MutexDequeQueueV3() = default;

  MutexDequeQueueV3(const MutexDequeQueueV3&) = delete;
  MutexDequeQueueV3& operator=(const MutexDequeQueueV3&) = delete;

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
      should_notify = queue_.empty() && waiting_consumers_ > 0;
      queue_.emplace_back(std::forward<Args>(args)...);
    }

    // V3: avoid redundant notify_one calls when no consumer is sleeping.
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
