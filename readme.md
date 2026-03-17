# yutil

High-performance C++ utility library for learning, self-implementation, and benchmarking. Focus areas: concurrent queues (mutex → ring → atomic MPMC), reproducible benchmarks, and small-step iteration with tests and causal analysis.

## Layout

| Directory     | Description |
|--------------|-------------|
| `ds/`        | Self-written data structures: blocking queues, bounded ring buffers, MPMC queues (mutex / atomic) |
| `benchmarks/`| Throughput benchmarks (Release, fixed scenarios, ns/op & Mops/s) |
| `tests/`     | Correctness tests for queue families (FIFO, close/wakeup, multi-thread) |
| `notes/`     | Design notes, iteration logs, interview one-pagers, payload matrix results |

## Queue Optimizations (Summary)

The queue work is split into three layers: (1) mutex + deque baseline tuning, (2) ring + mutex structural change, (3) bounded atomic MPMC. Each step has tests and Release benchmarks; optimization order: **wakeup policy → data structure → concurrency model**.

### 1. Mutex + Deque (SPSC & MPMC)

| Version | Change | Effect | Principle |
|---------|--------|--------|-----------|
| V1 | `notify_one()` on every `push()` | Lowest throughput | Redundant wakeups under contention |
| V2 | Notify only on empty→non-empty | Clear gain over V1 | Reduce synchronization noise first |
| V3 | Waiter-aware notify (track waiters) | Often best in this family | Fewer useless notifications when consumers are active |

**Benchmark (1P1C, 1M ops):** V1 ≈ 26.4 Mops/s → V2 ≈ 32.5 Mops/s → V3 ≈ 36.1 Mops/s (speedup vs V1 up to ~1.37x).

### 2. Ring + Mutex MPMC

| Version | Change | Effect | Principle |
|---------|--------|--------|-----------|
| V1 | Single CV for both empty/full | Usually worse than deque baseline | Wrong-party wakeups + lock contention |
| V2 | Split `not_empty` / `not_full` CVs | Better than V1 | Correct wait channels matter |
| V3 | Waiter-aware notifications | Can match or beat deque baseline | Fewer unnecessary wakeups |

Structure-only change is not enough; wait/wakeup policy sets the ceiling.

### 3. Bounded Atomic MPMC

| Version | Change | Effect | Principle |
|---------|--------|--------|-----------|
| V1 | Slot-sequence protocol, no padding/backoff | Often slower than mutex baseline in 4P4C | Atomic collision cost under contention |
| V2 | Cacheline separation (`alignas(64)`) | Not always better in current bench | Layout helps but not sufficient alone |
| V3 | Adaptive backoff after CAS failures | Clear gain over V1/V2 | Backoff reduces CAS thrashing |

Lock-free is not automatically faster; contention pattern and backoff policy dominate.

### 4. Payload × Concurrency Matrix (ns/op, median of 3 runs)

Implementations: `MutexDequeMpmcQueueBaseline(V2)`, `RingMutexMpmcQueueV2`, `BoundedAtomicMpmcQueueV2`. Scenarios: 1P1C, 2P2C, 4P4C, 8P8C.

| Payload        | 1P1C   | 2P2C   | 4P4C   | 8P8C   | Winner trend |
|----------------|--------|--------|--------|--------|--------------|
| `u64`          | Atomic best (≈14.6) | Atomic best (≈20.7) | Baseline best (≈42.7) | Baseline best (≈51.4) | Low contention: atomic; high: mutex baseline |
| `unique_ptr`   | Atomic best (≈65.9) | Atomic best (≈98.1) | Baseline best (≈145) | Baseline best (≈203) | Payload + contention shift advantage |
| Heavy (str+vec)| Atomic best (≈126) | Atomic best (≈107) | Atomic best (≈204) | Atomic best (≈262) | Heavy payload can favor atomic here |

**Takeaway:** No universal winner; choose by scenario and run matrix benchmarks on target workload.

## Build

This directory is wired into the workspace root `CMakeLists.txt`. Optional integration with cloned libraries: `abseil-cpp`, `benchmark`, `SPSCQueue`, `MPMCQueue`, `EASTL`, etc.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target <test_or_bench_target>
# Examples:
# ./build-release/yutil/mutex_queue_versions_test
# ./build-release/yutil/mutex_queue_versions_bench
# ./build-release/yutil/mpmc_queue_matrix_bench
```

Tests and benchmarks live under `yutil/tests/` and `yutil/benchmarks/`; each queue family has a corresponding test and (where applicable) benchmark executable.
