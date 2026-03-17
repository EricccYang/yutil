# MPMC Payload Matrix

This note captures the matrix benchmark for realistic payloads.

## Benchmark

- File: `yutil/benchmarks/mpmc_queue_matrix_bench.cpp`
- Build: `Release`
- Aggregation: median of 3 runs
- Implementations:
  - `MutexDequeMpmcQueueBaseline(V2)`
  - `RingMutexMpmcQueueV2`
  - `BoundedAtomicMpmcQueueV2`
- Scenarios:
  - `1P/1C`, `2P/2C`, `4P/4C`, `8P/8C`
- Payloads:
  - `u64`
  - `unique_ptr<uint64_t>`
  - `HeavyPayload` (`string + vector<uint64_t>`)

## Results Snapshot (ns/op)

| Payload | 1P1C | 2P2C | 4P4C | 8P8C | Winner Trend |
|---|---:|---:|---:|---:|---|
| `u64` | Atomic best (14.64) | Atomic best (20.68) | Baseline best (42.70) | Baseline best (51.38) | low contention: atomic; high contention: mutex baseline |
| `unique_ptr` | Atomic best (65.92) | Atomic best (98.05) | Baseline best (145.36) | Baseline best (203.48) | payload cost + contention shifts advantage |
| `heavy` | Atomic best (126.29) | Atomic best (106.51) | Atomic best (204.37) | Atomic best (262.24) | heavy payload still favors atomic here |

## Key Takeaways

1. There is no universal winner across all payloads and thread counts.
2. For tiny payload (`u64`), atomic queue wins at low contention but degrades faster as producer/consumer count rises.
3. For move-only and heavy payloads, atomic queue keeps a stronger edge in several scenarios, likely due reduced blocking wake overhead.
4. `RingMutexMpmcQueueV2` is consistently behind baseline in this matrix and needs more tuning before promotion.

## Practical Selection Rule (Current)

- If contention is moderate and payload is simple: start from `MutexDequeMpmcQueueBaseline(V2)`.
- If payload is expensive / move-heavy and thread count is not extreme: test `BoundedAtomicMpmcQueueV2` first.
- For very high contention (8P/8C), always re-benchmark on target workload before choosing.
