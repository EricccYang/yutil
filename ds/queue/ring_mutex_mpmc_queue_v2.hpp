#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace yutil::queue {

template <typename T>
class RingMutexMpmcQueueV2 {
 public:
  explicit RingMutexMpmcQueueV2(std::size_t capacity)
      : capacity_(capacity == 0 ? 1 : capacity), buffer_(capacity_) {}

  RingMutexMpmcQueueV2(const RingMutexMpmcQueueV2&) = delete;
  RingMutexMpmcQueueV2& operator=(const RingMutexMpmcQueueV2&) = delete;

  bool push(const T& value) { return emplace(value); }
  bool push(T&& value) { return emplace(std::move(value)); }
  bool try_push(const T& value) { return try_emplace(value); }
  bool try_push(T&& value) { return try_emplace(std::move(value)); }

  template <typename... Args>
  bool emplace(Args&&... args) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_cv_.wait(lock, [this] { return closed_ || size_ < capacity_; });
    if (closed_) {
      return false;
    }

    buffer_[tail_] = T(std::forward<Args>(args)...);
    tail_ = next(tail_);
    ++size_;

    lock.unlock();
    not_empty_cv_.notify_one();
    return true;
  }

  template <typename... Args>
  bool try_emplace(Args&&... args) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || size_ >= capacity_) {
      return false;
    }
    const bool was_empty = size_ == 0;
    buffer_[tail_] = T(std::forward<Args>(args)...);
    tail_ = next(tail_);
    ++size_;
    if (was_empty) {
      not_empty_cv_.notify_one();
    }
    return true;
  }

  bool wait_pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_cv_.wait(lock, [this] { return closed_ || size_ > 0; });
    if (size_ == 0) {
      return false;
    }

    out = std::move(buffer_[head_]);
    head_ = next(head_);
    --size_;

    lock.unlock();
    not_full_cv_.notify_one();
    return true;
  }

  bool try_pop(T& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) {
      return false;
    }
    out = std::move(buffer_[head_]);
    head_ = next(head_);
    --size_;
    return true;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();
  }

 private:
  std::size_t next(std::size_t index) const { return (index + 1) % capacity_; }

  const std::size_t capacity_;
  std::vector<T> buffer_;
  std::size_t head_{0};
  std::size_t tail_{0};
  std::size_t size_{0};

  std::mutex mutex_;
  std::condition_variable not_empty_cv_;
  std::condition_variable not_full_cv_;
  bool closed_{false};
};

}  // namespace yutil::queue
