#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace yutil {

template <typename T>
class SpscBoundedQueue {
 public:
  explicit SpscBoundedQueue(std::size_t capacity)
      : capacity_(normalize_capacity(capacity)),
        mask_(capacity_ - 1),
        storage_(new Storage[capacity_]) {}

  SpscBoundedQueue(const SpscBoundedQueue&) = delete;
  SpscBoundedQueue& operator=(const SpscBoundedQueue&) = delete;

  ~SpscBoundedQueue() {
    drain();
  }

  bool try_push(const T& value) { return emplace(value); }
  bool try_push(T&& value) { return emplace(std::move(value)); }

  template <typename... Args>
  bool emplace(Args&&... args) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto next = tail + 1;
    if (next - head_cache_ > capacity_) {
      head_cache_ = head_.load(std::memory_order_acquire);
      if (next - head_cache_ > capacity_) {
        return false;
      }
    }

    new (&storage_[tail & mask_]) T(std::forward<Args>(args)...);
    tail_.store(next, std::memory_order_release);
    return true;
  }

  bool try_pop(T& out) {
    const auto head = head_.load(std::memory_order_relaxed);
    if (tail_cache_ == head) {
      tail_cache_ = tail_.load(std::memory_order_acquire);
      if (tail_cache_ == head) {
        return false;
      }
    }

    T* slot = ptr(head);
    out = std::move(*slot);
    slot->~T();
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  std::size_t capacity() const { return capacity_; }

  bool empty() const {
    return size() == 0;
  }

  std::size_t size() const {
    const auto tail = tail_.load(std::memory_order_acquire);
    const auto head = head_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(tail - head);
  }

 private:
  using Storage = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

  static std::size_t normalize_capacity(std::size_t capacity) {
    std::size_t normalized = 1;
    while (normalized < capacity) {
      normalized <<= 1U;
    }
    return normalized < 2 ? 2 : normalized;
  }

  T* ptr(std::uint64_t index) {
    return std::launder(reinterpret_cast<T*>(&storage_[index & mask_]));
  }

  void drain() {
    auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_relaxed);
    while (head != tail) {
      ptr(head)->~T();
      ++head;
    }
  }

  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<Storage[]> storage_;

  alignas(64) std::atomic<std::uint64_t> head_{0};
  alignas(64) std::atomic<std::uint64_t> tail_{0};

  alignas(64) std::uint64_t head_cache_{0};
  alignas(64) std::uint64_t tail_cache_{0};
};

}  // namespace yutil
