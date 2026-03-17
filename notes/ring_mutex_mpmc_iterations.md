# Ring Mutex MPMC Iterations

This note tracks Group A: switching from `deque` to bounded ring while keeping mutex-based synchronization.

## Baseline

- Baseline alias: `yutil/ds/queue/mutex_deque_mpmc_queue_baseline.hpp`
- Baseline implementation: `MutexDequeMpmcQueueV2`

## Versions

### V1

- File: `yutil/ds/queue/ring_mutex_mpmc_queue_v1.hpp`
- Change: single condition-variable model for both producer/consumer paths
- Intent: minimal bounded ring conversion from deque design

### V2

- File: `yutil/ds/queue/ring_mutex_mpmc_queue_v2.hpp`
- Change: split `not_empty` / `not_full` condition-variables
- Intent: reduce wrong-party wakeups compared with V1

### V3

- File: `yutil/ds/queue/ring_mutex_mpmc_queue_v3.hpp`
- Change: waiter-aware notifications (`waiting_consumers_`, `waiting_producers_`)
- Intent: reduce unnecessary wakeups under contention

## Validation Artifacts

- Test: `yutil/tests/ring_mutex_mpmc_queue_versions_test.cpp`
- Bench: `yutil/benchmarks/ring_mutex_mpmc_queue_versions_bench.cpp`

## Benchmark Setup

- Producers/Consumers: `4P/4C`
- Items per producer: `1000` (shortened to keep V1 measurable)
- Total items: `4000`
- Build: `Release`

## Results

| Implementation | elapsed | ns/op | Mops/s | speedup vs baseline |
|---|---:|---:|---:|---:|
| `MutexDequeMpmcQueueBaseline(V2)` | 0.155 ms | 38.74 | 25.81 | 1.00x |
| `RingMutexMpmcQueueV1` | 0.301 ms | 75.29 | 13.28 | 0.51x |
| `RingMutexMpmcQueueV2` | 0.201 ms | 50.27 | 19.89 | 0.77x |
| `RingMutexMpmcQueueV3` | 0.154 ms | 38.54 | 25.95 | 1.01x |

## Analysis

- `V1` is clearly slower: single-CV coordination still creates avoidable wake contention.
- `V2` improves over `V1` by separating empty/full wake conditions.
- `V3` is best in Group A and approximately matches/slightly beats baseline in this short-run benchmark.

## Caveat

`V1` can become disproportionately slow under higher operation counts.  
For broader conclusions, rely more on the matrix benchmark (`mpmc_queue_matrix_bench`) where `RingMutexMpmcQueueV2` is compared across multiple thread ratios and payload types.

## Next Step

Use `RingMutexMpmcQueueV3` as the ring+mutex representative and compare against atomic queues under the same payload/thread matrix.
