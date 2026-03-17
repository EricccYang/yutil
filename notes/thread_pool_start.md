# 从高性能线程池开始

如果以线程池作为第一批自写目标，建议不要一开始就直接照着 `oneTBB` 或 `taskflow` 复刻，而是按复杂度分层推进。

## 为什么线程池适合作为起点

- 它同时覆盖并发、队列、内存分配、cache locality、异常传播、生命周期管理。
- 它和量化 / HPC 面试高度相关，经常会被延伸问到任务队列、work stealing、NUMA、绑定线程、尾延迟。
- 它可以自然带出后续的数据结构主题：bounded queue、lock-free queue、small task 优化、对象池。

## 建议的实现顺序

### v1: 固定线程池 + 中心队列

目标：

- 固定 `N` 个 worker 线程
- 一个共享任务队列
- `submit()` 返回 `future`
- 正确 shutdown
- 支持 `wait()` / 析构安全退出

推荐实现：

- 先用 `std::mutex + std::condition_variable + std::deque<std::function<void()>>`
- 先把正确性、API、异常传播做稳

### v2: 降低任务包装与分配开销

目标：

- 减少 `std::function` 带来的类型擦除与分配成本
- 评估 small task / move-only task 包装
- 评估批量提交

可参考：

- `thread-pool/include/BS_thread_pool.hpp`
- `folly` 的 executors 文档

### v3: 高性能任务队列

目标：

- 中心队列替换为更高性能的 bounded queue 或 MPMC queue
- 对比 blocking queue、lock-free queue、bounded queue 在吞吐和尾延迟上的差异

可参考：

- `concurrentqueue/concurrentqueue.h`
- `SPSCQueue/SPSCQueue.h`
- `MPMCQueue/MPMCQueue.h`

### v4: work-stealing

目标：

- 每个 worker 一个本地队列
- worker 空闲时去偷别人的任务
- 减少中心队列争用

可参考：

- `taskflow/taskflow.hpp`
- `oneTBB/include/oneapi/tbb/task_arena.h`
- `oneTBB/include/oneapi/tbb/task_group.h`

## 我建议你先读哪些实现

1. `thread-pool`
2. `folly` 的 `CPUThreadPoolExecutor`
3. `concurrentqueue`
4. `taskflow`
5. `oneTBB`

## 在本仓库中的落地建议

- `yutil/ds/thread_pool.hpp`: 第一版线程池
- `yutil/tests/thread_pool_test.cpp`: 生命周期、future、shutdown 测试
- `yutil/benchmarks/thread_pool_bench.cpp`: submit 开销、吞吐、空任务延迟
- `yutil/notes/thread_pool_start.md`: 持续补充设计权衡与 benchmark 结论

## 面试高频点

- 线程池为什么比频繁 `std::thread` 创建快
- 中心队列 vs work-stealing 的取舍
- 为什么尾延迟会比平均吞吐更难优化
- 锁 + 条件变量方案何时已经足够
- lock-free 队列是否一定更快
- false sharing、cache ping-pong、任务粒度过小的问题
