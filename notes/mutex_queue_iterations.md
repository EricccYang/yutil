# Mutex Deque Queue Iterations

This note tracks the first three small-step iterations of the simplest queue family:

- shared `std::deque`
- single `std::mutex`
- single `std::condition_variable`
- 1 producer / 1 consumer benchmark first

Code lives under `yutil/ds/queue/`.

## Versions

### V1

File:

- `yutil/ds/queue/mutex_deque_queue_v1.hpp`

Behavior:

- every successful `push()` does `notify_one()`

Intent:

- smallest correct blocking queue baseline

Cost:

- many notifications are redundant once the queue is already non-empty

### V2

File:

- `yutil/ds/queue/mutex_deque_queue_v2.hpp`

Change from V1:

- only `notify_one()` when the queue transitions from empty to non-empty

Why it can help:

- if the producer is faster than the consumer, the queue often stays non-empty
- waking the consumer again on every push adds synchronization overhead without new value

### V3

File:

- `yutil/ds/queue/mutex_deque_queue_v3.hpp`

Change from V2:

- track `waiting_consumers_`
- only `notify_one()` when the queue was empty and a consumer is actually sleeping

Why it can help:

- when the consumer is actively draining, notification is unnecessary
- this removes more redundant condition-variable traffic

Tradeoff:

- one extra counter under the same mutex
- still worth it in this benchmark because avoiding redundant notification costs more than the counter update

## Correctness validation

Test file:

- `yutil/tests/mutex_queue_versions_test.cpp`

Coverage:

- FIFO behavior
- `close()` wakeup behavior
- 2-thread producer / consumer order validation

## Release benchmark

Benchmark file:

- `yutil/benchmarks/mutex_queue_versions_bench.cpp`

Command:

- `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-release --target mutex_queue_versions_test mutex_queue_versions_bench`
- `./build-release/yutil/mutex_queue_versions_test`
- `./build-release/yutil/mutex_queue_versions_bench`

Observed result:

| Implementation | elapsed | ns/op | Mops/s | speedup vs V1 |
|---|---:|---:|---:|---:|
| `MutexDequeQueueV1` | 37.914 ms | 37.91 | 26.38 | 1.00x |
| `MutexDequeQueueV2` | 30.800 ms | 30.80 | 32.47 | 1.23x |
| `MutexDequeQueueV3` | 27.729 ms | 27.73 | 36.06 | 1.37x |

## Analysis

### Why V2 beats V1

The biggest waste in V1 is unconditional notification.

In a 1 producer / 1 consumer throughput run, the queue is often already non-empty.  
That means the consumer is either:

- already awake and draining, or
- already guaranteed to wake because the queue is non-empty

So `notify_one()` on every push is mostly redundant. V2 removes that waste by only notifying on the empty-to-non-empty transition.

### Why V3 beats V2

V2 still notifies whenever the queue was empty before a push, even if the consumer is not actually sleeping at that moment.

V3 adds a tiny bit of state:

- `waiting_consumers_`

That lets the producer skip notification if no consumer is blocked in `wait_pop()`.

This is still a small optimization, but it improves the steady-state path enough to show another step forward.

### Why all three are still fundamentally limited

They all share the same structural bottlenecks:

- one mutex for both producer and consumer
- one shared deque
- blocking wakeup path centered on `condition_variable`

So even the best mutex/deque version is still mostly about trimming avoidable synchronization overhead, not changing the big-O behavior of the design.

## What this suggests for the next step

Once the deque/mutex family is understood, the natural next move is not another tiny tweak.

The real next step is changing the queue architecture:

1. bounded ring buffer
2. SPSC atomic indices
3. then MPMC or work-stealing

That is where the next major performance jump should come from.
