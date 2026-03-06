# 20260306-docs-trace-backpressure

Git Commit Message:
docs(trace): 补充 trace 背压与重试状态机设计记录

Modification:
- docs/dev-log/20260306-docs-trace-backpressure.md

Learning Tips:
Newbie Tips:
- 背压不是“队列满了回个 503”这么简单，关键是先分清入口准入、内存积压、下游投递失败这三层语义。
- `202 Accepted` 一旦返回，就表示服务端已经接下责任；后续即使线程池满了，也不能把这笔账再甩回客户端。
- Trace 聚合不能依赖 TCP 连接语义；同一条 Trace 的多个 span 可能来自不同服务、不同进程、不同连接。

Function Explanation:
- `TraceSessionManager::Push(...)`：负责把 span 纳入聚合态，适合做入口准入判断与实时积压计数。
- `TraceSessionManager::Dispatch(...)`：负责把已 ready 的 trace 从聚合态摘出并尝试投递到线程池，必须显式处理 `submit` 失败回滚。
- `ThreadPool::submit(...)`：返回 `false` 代表下游队列已满；如果上游已经把 session 摘掉又不回滚，就会直接丢 trace。

Pitfalls:
- 把“收集超时”和“分发失败重试”混用同一套时间语义，会让 ready trace 被错误地当成“还在等新 span”的普通 session。
- 只用 `bool` 表达 `Push` 结果，后续很难区分“正常接收”“过载拒绝”“已接收但延后投递”等不同状态。
- 统一顺延整个时间轮看起来省事，实际上会把已经 ready 的 trace 一起憋在内存里，放大积压并制造重试尖峰。

---

追加记录（当前设计结论）:

Git Commit Message:
docs(trace): 明确 trace 背压最小落地方案

Modification:
- docs/dev-log/20260306-docs-trace-backpressure.md

Learning Tips:
Newbie Tips:
- 第一版阈值可以先硬编码，不必一开始就做成完美配置系统；但状态机语义必须先定准。
- 三档水位比单纯高低水位更适合 Trace 聚合场景：正常、过载保老 trace、极限全拒。

Function Explanation:
- `total_buffered_spans`：主看入口内存积压，应该在 `Push/Dispatch/submit失败回滚` 的真实时刻实时加减。
- `active_sessions`：辅助保护指标，防止大量碎片化 trace 把索引和时间轮节点撑爆。
- `pendingTasks()`：线程池下游堵塞信号，100% 基数应取队列容量，不是 worker 线程数。

Pitfalls:
- 误把 `kernel_worker_threads` 当成 `pending_tasks` 的容量基数，会把“消费速度”和“队列容量”两个概念混在一起。
- 误把 `kernel_io_buffer` 直接当作 Trace 聚合内存上限，会把网络 IO 缓冲和业务对象堆内存混为一谈。
