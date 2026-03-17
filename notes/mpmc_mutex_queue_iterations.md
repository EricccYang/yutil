# MPMC Mutex Queue Iterations

This note tracks a small-step MPMC iteration over a shared `deque + mutex + condition_variable` design.

## Versions

### V1

File:

- `yutil/ds/queue/mutex_deque_mpmc_queue_v1.hpp`

Behavior:

- every successful `push()` calls `notify_one()`

### V2

File:

- `yutil/ds/queue/mutex_deque_mpmc_queue_v2.hpp`

Change:

- only notify on empty -> non-empty transition

Hypothesis:

- less wakeup traffic than V1
- but may underutilize multiple consumers during bursts

### V3

File:

- `yutil/ds/queue/mutex_deque_mpmc_queue_v3.hpp`

Change:

- track `waiting_consumers_`
- notify only when there is at least one blocked consumer

Hypothesis:

- avoids redundant notify calls when consumers are already active
- avoids V2's under-notify behavior when queue is non-empty and waiters exist

## Validation Artifacts

- baseline alias: `yutil/ds/queue/mutex_deque_mpmc_queue_baseline.hpp`
- test: `yutil/tests/mpmc_mutex_queue_versions_test.cpp`
- benchmark: `yutil/benchmarks/mpmc_mutex_queue_versions_bench.cpp`

## Benchmark Setup

- 4 producers, 4 consumers
- 250,000 items per producer
- total items: 1,000,000
- build type: `Release`

## Results

Command:

- `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-release --target mpmc_mutex_queue_versions_test mpmc_mutex_queue_versions_bench`
- `./build-release/yutil/mpmc_mutex_queue_versions_test`
- `./build-release/yutil/mpmc_mutex_queue_versions_bench`

Observed on this machine:

| Implementation | elapsed | ns/op | Mops/s | speedup vs V1 |
|---|---:|---:|---:|---:|
| `MutexDequeMpmcQueueV1` | 89.942 ms | 89.94 | 11.12 | 1.00x |
| `MutexDequeMpmcQueueV2` | 47.443 ms | 47.44 | 21.08 | 1.90x |
| `MutexDequeMpmcQueueV3` | 62.310 ms | 62.31 | 16.05 | 1.44x |

## Analysis

### Why V2 beats V1

In MPMC with 4 producers and 4 consumers, V1's unconditional `notify_one()` creates heavy wakeup traffic.

V2 removes most redundant notifications by only waking on empty -> non-empty transitions.  
That sharply cuts synchronization noise and gives the largest gain in this round.

### Why V3 is slower than V2

V3 notifies whenever `waiting_consumers_ > 0`. Under contention, that condition is frequently true.

So compared with V2:

- V3 sends more notifications on non-empty queue states
- this reintroduces wakeup overhead
- the extra waiting counter updates also add a small lock-protected bookkeeping cost

Net effect in this benchmark: V3 is still better than V1, but loses to V2.

### What we learned

- for this mutex+deque MPMC family, reducing notification frequency matters a lot
- in this workload, V2's simple rule is the best tradeoff so far
- more \"smart\" wakeup logic is not always better if it increases notify frequency under contention

### Next small steps

1. Keep V2 as the current best mutex baseline.
2. Try bounded ring-buffer + mutex MPMC (`v1/v2/v3`) to reduce allocator / node overhead from `deque`.
3. Then move to atomic-queue architectures.
