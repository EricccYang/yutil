#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace yutil::queue {

template <typename T>
class MutexDequeQueueV2 {
 public:
  MutexDequeQueueV2() = default;

  MutexDequeQueueV2(const MutexDequeQueueV2&) = delete;
  MutexDequeQueueV2& operator=(const MutexDequeQueueV2&) = delete;

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
      should_notify = queue_.empty();
      queue_.emplace_back(std::forward<Args>(args)...);
    }

    // V2: wake only on empty -> non-empty transition.
    if (should_notify) {
      cv_.notify_one();
    }
    return true;
  }

  bool wait_pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
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
  bool closed_{false};
};

}  // namespace yutil::queue
