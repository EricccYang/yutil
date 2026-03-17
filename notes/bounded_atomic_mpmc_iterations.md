# Bounded Atomic MPMC Iterations

This note tracks Group B: bounded atomic MPMC queue iterations.

## Baseline

- Baseline alias: `yutil/ds/queue/mutex_deque_mpmc_queue_baseline.hpp`
- Baseline implementation: `MutexDequeMpmcQueueV2`

## Versions

### V1

- File: `yutil/ds/queue/bounded_atomic_mpmc_queue_v1.hpp`
- Change: baseline bounded MPMC slot-sequence protocol (no extra padding/backoff)

### V2

- File: `yutil/ds/queue/bounded_atomic_mpmc_queue_v2.hpp`
- Change: cacheline separation for counters/cells (`alignas(64)`)

### V3

- File: `yutil/ds/queue/bounded_atomic_mpmc_queue_v3.hpp`
- Change: simple adaptive backoff (`yield` after repeated CAS failures)

## Validation Artifacts

- Test: `yutil/tests/bounded_atomic_mpmc_queue_versions_test.cpp`
- Bench: `yutil/benchmarks/bounded_atomic_mpmc_queue_versions_bench.cpp`

## Benchmark Setup

- Producers/Consumers: `4P/4C`
- Items per producer: `250000`
- Total items: `1000000`
- Build: `Release`

## Results

| Implementation | elapsed | ns/op | Mops/s | speedup vs baseline |
|---|---:|---:|---:|---:|
| `MutexDequeMpmcQueueBaseline(V2)` | 40.963 ms | 40.96 | 24.41 | 1.00x |
| `BoundedAtomicMpmcQueueV1` | 161.280 ms | 161.28 | 6.20 | 0.25x |
| `BoundedAtomicMpmcQueueV2` | 174.250 ms | 174.25 | 5.74 | 0.24x |
| `BoundedAtomicMpmcQueueV3` | 66.922 ms | 66.92 | 14.94 | 0.61x |
| `rigtorp::MPMCQueue` | 102.499 ms | 102.50 | 9.76 | 0.40x |

## Analysis

- In this environment/workload, mutex baseline remains faster than current atomic prototypes.
- `V2` does not improve in this run; current alignment-only change is not enough for this contention pattern.
- `V3` is significantly better than `V1/V2`, suggesting backoff helps reduce CAS collision cost here.
- `rigtorp::MPMCQueue` is faster than `V1` but still behind baseline here, showing workload sensitivity.

## What This Means

- Atomic MPMC is not automatically faster; details of contention strategy and memory layout dominate.
- Current atomic queue needs further tuning (slot stride, ticket scheme, backoff policy) before replacing baseline in general use.

## Next Step

Use matrix benchmark (`mpmc_queue_matrix_bench`) to choose by scenario:

- light `T`, low contention: atomic can win
- higher contention / some payload mixes: mutex baseline may still be preferable
