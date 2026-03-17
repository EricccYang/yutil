#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace yutil {

template <typename T>
class MutexBlockingQueue {
 public:
  MutexBlockingQueue() = default;

  MutexBlockingQueue(const MutexBlockingQueue&) = delete;
  MutexBlockingQueue& operator=(const MutexBlockingQueue&) = delete;

  bool push(const T& value) { return emplace(value); }
  bool push(T&& value) { return emplace(std::move(value)); }

  template <typename... Args>
  bool emplace(Args&&... args) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return false;
      }
      queue_.emplace_back(std::forward<Args>(args)...);
    }
    cv_.notify_one();
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

  std::optional<T> wait_pop() {
    T value;
    if (!wait_pop(value)) {
      return std::nullopt;
    }
    return value;
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

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<T> queue_;
  bool closed_{false};
};

}  // namespace yutil
