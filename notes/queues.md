# Queue Iteration Plan

This note starts the queue-first path for `yutil`.

## Why queues first

Queues are the smallest useful unit for the thread-pool roadmap:

- a mutex queue is the easiest correct baseline
- a bounded SPSC queue is the first atomic queue worth mastering
- queue benchmarks expose contention, wakeup cost, cache locality, and memory-order choices
- thread pools become much easier after queue semantics are clear

## Queue implementations already present in this workspace

### SPSC

- `folly/folly/ProducerConsumerQueue.h`
- `SPSCQueue/include/rigtorp/SPSCQueue.h`

### MPMC

- `folly/folly/MPMCQueue.h`
- `concurrentqueue/concurrentqueue.h`
- `concurrentqueue/blockingconcurrentqueue.h`
- `MPMCQueue/include/rigtorp/MPMCQueue.h`
- `oneTBB/include/oneapi/tbb/concurrent_queue.h`
- `yalantinglibs/include/ylt/util/concurrentqueue.h`

### Scheduler / work-stealing related

- `taskflow/taskflow/core/wsq.hpp`
- `oneTBB/include/oneapi/tbb/task_arena.h`
- `folly/folly/executors/task_queue/LifoSemMPMCQueue.h`

## First yutil queue iteration

### Implemented now

- `yutil/ds/mutex_blocking_queue.hpp`
- `yutil/ds/spsc_bounded_queue.hpp`
- `yutil/ds/queue/mutex_deque_queue_v1.hpp`
- `yutil/ds/queue/mutex_deque_queue_v2.hpp`
- `yutil/ds/queue/mutex_deque_queue_v3.hpp`
- `yutil/ds/queue/mutex_deque_mpmc_queue_v1.hpp`
- `yutil/ds/queue/mutex_deque_mpmc_queue_v2.hpp`
- `yutil/ds/queue/mutex_deque_mpmc_queue_v3.hpp`
- `yutil/ds/queue/mutex_deque_mpmc_queue_baseline.hpp`
- `yutil/ds/queue/ring_mutex_mpmc_queue_v1.hpp`
- `yutil/ds/queue/ring_mutex_mpmc_queue_v2.hpp`
- `yutil/ds/queue/ring_mutex_mpmc_queue_v3.hpp`
- `yutil/ds/queue/bounded_atomic_mpmc_queue_v1.hpp`
- `yutil/ds/queue/bounded_atomic_mpmc_queue_v2.hpp`
- `yutil/ds/queue/bounded_atomic_mpmc_queue_v3.hpp`
- `yutil/tests/queue_test.cpp`
- `yutil/tests/mutex_queue_versions_test.cpp`
- `yutil/tests/mpmc_mutex_queue_versions_test.cpp`
- `yutil/tests/ring_mutex_mpmc_queue_versions_test.cpp`
- `yutil/tests/bounded_atomic_mpmc_queue_versions_test.cpp`
- `yutil/tests/mpmc_queue_payload_test.cpp`
- `yutil/benchmarks/queue_bench.cpp`
- `yutil/benchmarks/mutex_queue_versions_bench.cpp`
- `yutil/benchmarks/mpmc_mutex_queue_versions_bench.cpp`
- `yutil/benchmarks/ring_mutex_mpmc_queue_versions_bench.cpp`
- `yutil/benchmarks/bounded_atomic_mpmc_queue_versions_bench.cpp`
- `yutil/benchmarks/mpmc_queue_matrix_bench.cpp`

### Intent of each queue

- `MutexBlockingQueue`: correctness-first baseline for later thread-pool work
- `SpscBoundedQueue`: minimal lock-free queue to study cache padding and acquire/release protocols
- `MutexDequeQueueV1/V2/V3`: three small-step iterations over the simplest mutex + deque design, with performance diffs documented in `yutil/notes/mutex_queue_iterations.md`
- `MutexDequeMpmcQueueV1/V2/V3`: MPMC-focused small-step iterations over mutex + deque, with diffs documented in `yutil/notes/mpmc_mutex_queue_iterations.md`
- `MutexDequeMpmcQueueBaseline`: frozen MPMC baseline alias (currently points to V2)
- `RingMutexMpmcQueueV1/V2/V3`: structure-change iteration (deque -> ring) while keeping mutex model, documented in `yutil/notes/ring_mutex_mpmc_iterations.md`
- `BoundedAtomicMpmcQueueV1/V2/V3`: atomic bounded MPMC iteration, documented in `yutil/notes/bounded_atomic_mpmc_iterations.md`
- `mpmc_queue_matrix_bench`: scenario and payload matrix benchmark, summarized in `yutil/notes/mpmc_payload_matrix.md`

## Benchmark scope for iteration 1

Current benchmark compares 1 producer / 1 consumer throughput for:

- `yutil::MutexBlockingQueue`
- `yutil::SpscBoundedQueue`
- `rigtorp::SPSCQueue`
- `moodycamel::BlockingConcurrentQueue`

Measured output:

- total elapsed time
- nanoseconds per item
- million ops per second

This is intentionally simple and timing-focused first.

### Current Release results

Command:

- `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-release --target yutil_tests yutil_benchmarks`
- `./build-release/yutil/yutil_benchmarks`

Observed on this machine:

| Implementation | elapsed | ns/op | Mops/s |
|---|---:|---:|---:|
| `yutil::MutexBlockingQueue` | 40.010 ms | 40.01 | 24.99 |
| `yutil::SpscBoundedQueue` | 18.339 ms | 18.34 | 54.53 |
| `rigtorp::SPSCQueue` | 15.150 ms | 15.15 | 66.01 |
| `moodycamel::BlockingConcurrentQueue` | 51.451 ms | 51.45 | 19.44 |

Takeaway:

- the first lock-free `SpscBoundedQueue` is already much faster than the mutex baseline
- `rigtorp::SPSCQueue` is still ahead, so it is a strong implementation to read before iteration 2
- blocking MPMC queue designs are not ideal baselines for pure SPSC throughput

## Next queue iterations

1. Tune atomic MPMC slot strategy (stride/sequence protocol variants) and re-test against `rigtorp::MPMCQueue`.
2. Add queue-level allocator/object-pool options for heavy payloads.
3. Add work-stealing deque prototype for thread-pool workers.
4. Add latency-focused reporting (p50/p95/p99) in addition to throughput.

## macOS profiling note

This machine currently does not have `perf`, `xctrace`, or `instruments` available in `PATH`.

For now, timing benchmarks are the easiest path.
If you later install full Xcode tools, `xctrace` / Instruments are the natural next step on macOS.

Available now:

- `sample`
- `dtrace`
- `atos`

So the practical profiling path on this machine is:

1. start with Release timing benchmarks
2. use `sample` for stack sampling
3. move to `xctrace` / Instruments after full Xcode tooling is installed
