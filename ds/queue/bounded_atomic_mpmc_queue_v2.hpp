#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace yutil::queue {

template <typename T>
class BoundedAtomicMpmcQueueV2 {
 public:
  explicit BoundedAtomicMpmcQueueV2(std::size_t capacity)
      : capacity_(normalize_capacity(capacity)),
        mask_(capacity_ - 1),
        cells_(new Cell[capacity_]) {
    for (std::size_t i = 0; i < capacity_; ++i) {
      cells_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  BoundedAtomicMpmcQueueV2(const BoundedAtomicMpmcQueueV2&) = delete;
  BoundedAtomicMpmcQueueV2& operator=(const BoundedAtomicMpmcQueueV2&) = delete;

  ~BoundedAtomicMpmcQueueV2() { drain_remaining(); }

  bool close() { return !closed_.exchange(true, std::memory_order_release); }

  bool is_closed() const { return closed_.load(std::memory_order_acquire); }

  bool try_push(const T& value) { return emplace(value); }
  bool try_push(T&& value) { return emplace(std::move(value)); }

  template <typename... Args>
  bool emplace(Args&&... args) {
    if (is_closed()) {
      return false;
    }

    std::size_t pos = enqueue_pos_.value.load(std::memory_order_relaxed);
    for (;;) {
      Cell& cell = cells_[pos & mask_];
      const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
      const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
      if (dif == 0) {
        if (enqueue_pos_.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          new (&cell.storage) T(std::forward<Args>(args)...);
          cell.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
      } else if (dif < 0) {
        return false;
      } else {
        pos = enqueue_pos_.value.load(std::memory_order_relaxed);
      }
    }
  }

  bool try_pop(T& out) {
    std::size_t pos = dequeue_pos_.value.load(std::memory_order_relaxed);
    for (;;) {
      Cell& cell = cells_[pos & mask_];
      const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
      const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
      if (dif == 0) {
        if (dequeue_pos_.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          T* slot = std::launder(reinterpret_cast<T*>(&cell.storage));
          out = std::move(*slot);
          slot->~T();
          cell.sequence.store(pos + capacity_, std::memory_order_release);
          return true;
        }
      } else if (dif < 0) {
        return false;
      } else {
        pos = dequeue_pos_.value.load(std::memory_order_relaxed);
      }
    }
  }

 private:
  using Storage = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

  struct alignas(64) Cell {
    std::atomic<std::size_t> sequence;
    Storage storage;
  };

  struct alignas(64) AlignedCounter {
    std::atomic<std::size_t> value{0};
  };

  static std::size_t normalize_capacity(std::size_t capacity) {
    std::size_t normalized = 1;
    while (normalized < capacity) {
      normalized <<= 1U;
    }
    return normalized < 2 ? 2 : normalized;
  }

  void drain_remaining() {
    T value;
    while (try_pop(value)) {
    }
  }

  const std::size_t capacity_;
  const std::size_t mask_;
  std::unique_ptr<Cell[]> cells_;
  AlignedCounter enqueue_pos_;
  AlignedCounter dequeue_pos_;
  std::atomic<bool> closed_{false};
};

}  // namespace yutil::queue
