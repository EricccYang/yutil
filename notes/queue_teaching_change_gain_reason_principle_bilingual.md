# Queue Optimization Teaching Notes (Bilingual)
# 队列优化教学笔记（中英双语）

This note summarizes the queue iterations in a teaching format:
change -> gain -> reason -> principle.
本笔记按教学结构总结队列迭代：改变 -> 收益 -> 原因 -> 原理。

---

## 1) How to Read the Results
## 1）如何解读结果

- **CN**: `ns/op` 越低越好，`Mops/s` 越高越好。  
  **EN**: Lower `ns/op` is better; higher `Mops/s` is better.
- **CN**: 同一组 benchmark 内可横向比较，不同 benchmark 文件之间只做趋势参考。  
  **EN**: Compare directly within the same benchmark group; across benchmark files use trend-level comparison.
- **CN**: 结果是场景相关的，不存在“永远最快”的队列。  
  **EN**: Results are scenario-dependent; there is no universally fastest queue.

---

## 2) Baseline: Mutex + Deque MPMC
## 2）基线：Mutex + Deque MPMC

### V1 -> V2 -> V3 (mutex_deque_mpmc_queue)

| Change / 改变 | Gain / 收益 | Reason / 原因 | Principle / 原理 |
|---|---|---|---|
| V1: always `notify_one()` / 每次 push 都通知 | Lowest throughput / 吞吐最低 | Redundant wakeups under contention / 高争用下大量冗余唤醒 | Wakeup traffic is a real bottleneck / 唤醒流量本身就是瓶颈 |
| V2: notify only on empty->non-empty / 仅空转非空时通知 | Large improvement / 显著提升 | Fewer useless notifications / 减少无效通知 | Reduce synchronization noise first / 先降低同步噪音 |
| V3: waiter-aware notify / 等待者感知通知 | Better than V1, may lose to V2 in some loads / 好于V1，部分负载下不如V2 | Bookkeeping + higher notify frequency in some patterns / 额外计数与部分模式下更高通知频率 | “Smarter” logic can be slower if it triggers more synchronization / 更“聪明”的逻辑若带来更多同步，可能更慢 |

---

## 3) Group A: Ring + Mutex MPMC
## 3）A组：Ring + Mutex MPMC

### What changed / 做了什么

- **CN**: 把 `deque` 改成固定容量 `ring`，并继续保持 mutex 模型。  
  **EN**: Replaced `deque` with fixed-capacity ring while keeping mutex-based synchronization.
- **CN**: 从单条件变量到空/满分离，再到等待者感知通知。  
  **EN**: Evolved from single condition variable to split empty/full conditions, then waiter-aware notifications.

### Change -> Gain -> Reason -> Principle

| Change / 改变 | Gain / 收益 | Reason / 原因 | Principle / 原理 |
|---|---|---|---|
| V1 single-CV ring / 单CV ring | Usually poor / 通常较差 | Wrong-party wakeups + lock contention / 唤醒错配 + 锁竞争 | Structure change alone is not enough / 仅改结构不够 |
| V2 split not-empty/not-full / 分离空满条件变量 | Better than V1 / 明显好于V1 | Less wake mismatch / 减少唤醒错配 | Correct wait channels matter / 等待通道设计关键 |
| V3 waiter-aware on top of V2 / 在V2上加等待者感知 | Best in Group A / A组最优 | Fewer unnecessary notifications / 进一步减少无效通知 | Notification policy is first-class optimization / 通知策略是一级优化对象 |

---

## 4) Group B: Bounded Atomic MPMC
## 4）B组：Bounded Atomic MPMC

### What changed / 做了什么

- **CN**: 引入每槽位 sequence 协议的 bounded atomic MPMC。  
  **EN**: Introduced bounded atomic MPMC with per-slot sequence protocol.
- **CN**: v2 增加 cacheline 分离；v3 增加退避策略。  
  **EN**: v2 adds cacheline separation; v3 adds backoff strategy.

### Change -> Gain -> Reason -> Principle

| Change / 改变 | Gain / 收益 | Reason / 原因 | Principle / 原理 |
|---|---|---|---|
| V1 naive atomic MPMC / 初版 atomic MPMC | Often below mutex baseline / 常低于mutex基线 | CAS collision + retry churn / CAS 冲突与重试开销 | Lock-free is not automatically faster / 无锁不等于自动更快 |
| V2 add alignment / 加cacheline对齐 | Mixed gains / 收益不稳定 | Helps false sharing but not full contention policy / 只解决伪共享，未解决争用策略 | Micro-optimizations need matching contention control / 微优化需配套争用控制 |
| V3 backoff / 退避策略 | Better than V1/V2 in current runs / 当前结果优于V1/V2 | Reduced hot-loop collision pressure / 降低热点自旋冲突 | In high contention, scheduling strategy can dominate / 高争用下调度策略可主导性能 |

---

## 5) Real Payload Matrix (u64 / unique_ptr / heavy)
## 5）真实负载矩阵（u64 / unique_ptr / heavy）

### Observed pattern / 观察模式

- **CN**: `u64` 下，低并发 atomic 常胜；线程数上去后 mutex baseline 更稳。  
  **EN**: For `u64`, atomic often wins at low contention; mutex baseline becomes more stable as thread count increases.
- **CN**: `unique_ptr` 下，atomic 在低并发优势更明显，但高并发时 baseline 可能反超。  
  **EN**: For `unique_ptr`, atomic has clearer low-contention advantage, but baseline may catch up or win at high contention.
- **CN**: `heavy` 负载下，atomic 在多个场景里表现更优。  
  **EN**: For heavy payloads, atomic performs better in multiple scenarios.

### Why payload changes outcomes / 为什么 payload 会改变结论

- **CN**: 当 `T` 很轻（如 `u64`）时，同步开销占比高，锁争用和唤醒策略更关键。  
  **EN**: When `T` is light (`u64`), synchronization overhead dominates; lock contention and wake policy matter more.
- **CN**: 当 `T` 变重（move-only 或大对象）时，构造/移动/析构与内存访问模式更影响整体。  
  **EN**: With heavier `T` (move-only or large objects), construction/move/destruction and memory access patterns become major factors.
- **CN**: 这就是“算法收益”和“payload 成本”必须分开看的原因。  
  **EN**: This is why algorithmic gain and payload cost must be analyzed separately.

---

## 6) Practical Rules for Real Projects
## 6）真实项目中的实用规则

1. **CN**: 先定场景再选队列（线程比、payload、容量）。  
   **EN**: Define workload first (thread ratio, payload, capacity), then choose queue.
2. **CN**: 先用稳定 baseline，再引入更激进版本做 A/B。  
   **EN**: Keep a stable baseline, then A/B against more aggressive versions.
3. **CN**: 每次只改一件事并记录因果链。  
   **EN**: Change one thing per iteration and document the causal chain.
4. **CN**: 复杂 `T` 必测，不要只用 `int` 推断生产表现。  
   **EN**: Always test complex `T`; do not infer production behavior from `int` only.
5. **CN**: 结果波动时优先看中位值和矩阵，而不是单次峰值。  
   **EN**: When noisy, trust medians and matrix trends, not single-run peaks.

---

## 7) What to Teach Juniors
## 7）给初学者的教学重点

- **CN**: “更复杂”不等于“更快”，先证明收益再接受复杂度。  
  **EN**: “More complex” does not mean “faster”; prove gains before accepting complexity.
- **CN**: 队列优化的第一性问题是：你在省什么成本（锁、唤醒、cache、分配、重试）？  
  **EN**: First-principles question: what cost are you reducing (locks, wakeups, cache misses, allocations, retries)?
- **CN**: 任何优化都要配套测试和可复现 benchmark。  
  **EN**: Every optimization needs matching tests and reproducible benchmarks.

---

## 8) Reference Files
## 8）关联文件

- `yutil/notes/mutex_queue_iterations.md`
- `yutil/notes/mpmc_mutex_queue_iterations.md`
- `yutil/notes/ring_mutex_mpmc_iterations.md`
- `yutil/notes/bounded_atomic_mpmc_iterations.md`
- `yutil/notes/mpmc_payload_matrix.md`
