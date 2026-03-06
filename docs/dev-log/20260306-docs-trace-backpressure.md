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

---

追加记录（状态枚举收口第一刀）:

Git Commit Message:
refactor(trace): 收口背压状态枚举并升级 Push 返回语义

Modification:
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/handlers/LogHandler.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- server/tests/TraceSessionManager_integration_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260306-docs-trace-backpressure.md

Learning Tips:
Newbie Tips:
- 当一个接口后续注定会承载多种业务结果时，尽早把 `bool` 升级成枚举，比后面到处补注释靠谱得多。
- “先把状态名和语义钉死，再往里填判断逻辑”能显著减少并发状态机开发时的返工。

Function Explanation:
- `TraceSession::LifecycleState`：区分“还在收集 span”和“ready 但等待重投”两种 session 语义，避免继续共用同一套 timeout 含义。
- `TraceSessionManager::PushResult`：为 `LogHandler` 提供明确返回语义，后续可承接 `Accepted / RejectedOverload / AcceptedDeferred`。
- `TraceSessionManager::OverloadState`：为后续多指标水位驱动的系统门禁状态预留统一表达。

Pitfalls:
- 只改头文件枚举不改调用点，编译会直接炸；尤其测试里大量 `ASSERT_TRUE(manager.Push(...))` 需要同步迁移。
- 当前只是把返回值和状态枚举立住，实际背压判断还没接入；如果现在就误以为 overload 生效了，会把排障方向带偏。

---

追加记录（准入门禁与新老 Trace 区分放行）:

Git Commit Message:
feat(trace): 接入三档水位准入门禁并区分新老trace放行

Modification:
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/threadpool/ThreadPool.h
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260306-docs-trace-backpressure.md

Learning Tips:
Newbie Tips:
- `pending_tasks` 的 100% 基数应该取线程池队列容量，不是 worker 线程数；线程数决定的是消化速度，不是桶有多大。
- 第一版背压阈值完全可以先硬编码，但每个指标都要有自己的 hard limit，别偷懒共用一个魔法数字。

Function Explanation:
- `ThreadPool::maxQueueSize()`：暴露线程池任务队列硬上限，供 Trace 背压计算 `pending_tasks` 水位基数。
- `TraceSessionManager::RefreshOverloadState()`：根据 `total_buffered_spans / active_sessions / pending_tasks` 三个指标统一刷新 `Normal / Overload / Critical`。
- `TraceSessionManager::ShouldRejectIncomingTrace(...)`：把“Overload 拒新 trace、Critical 新老都拒”的门禁规则集中收口，避免散在 `Push` 里写一坨分支。

Pitfalls:
- 入口门禁要在“新建 session 之前”判断，否则你明明想拒绝新 trace，却先把 session 和计数建出来，账会直接脏掉。
- `Dispatch` 从内存移除 session 时如果不顺手更新实时计数，overload 状态会一直卡高，后续请求会被假性拒绝。
- 当前这一步只做了准入门禁，`submit` 失败回滚和 ready 重试还没接；如果线程池提交失败，仍然需要下一刀补齐兜底逻辑。

---

追加记录（watermark 预计算缓存）:

Git Commit Message:
refactor(trace): 将背压水位阈值改为构造期预计算缓存

Modification:
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260306-docs-trace-backpressure.md

Learning Tips:
Newbie Tips:
- 如果阈值在运行期不会变化，就不要在热路径里反复从比例换算成绝对值；直接构造期缓存，热路径只做比较。
- 优化热路径时先盯“会不会重复做纯函数计算”，这种地方虽然不致命，但属于白给的开销。

Function Explanation:
- `TraceSessionManager::BuildWatermark(...)`：把 hard limit 换算成 `low/high/critical` 三档阈值，当前只在构造阶段调用。
- `buffered_span_watermark_ / active_session_watermark_ / pending_task_watermark_`：缓存后的固定阈值，供 `RefreshOverloadState()` 直接读取。

Pitfalls:
- 在匿名命名空间里返回类的私有嵌套类型，容易撞上可见性/访问控制问题；这类 helper 更适合做成类内静态私有函数。
- 只有当 hard limit 或线程池容量会运行时变化时，watermark 才需要重算；否则在热路径里现算就是纯重复劳动。

---

追加记录（submit 失败回滚与最小延迟重投）:

Git Commit Message:
fix(trace): submit失败时回滚session并返回accepteddeferred

Modification:
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260306-docs-trace-backpressure.md

Learning Tips:
Newbie Tips:
- 一旦请求层已经准备返回 `202`，后台异步提交失败就不能简单丢数据；最少也要回滚到内存态，保持“已接收即由服务端负责”的语义成立。
- `std::function` 要求可拷贝时，直接把 `unique_ptr` 塞进任务里会很别扭；如果还要支持 submit 失败回滚，可以先用“可拷贝 holder 持有 unique_ptr”的方式过渡。

Function Explanation:
- `TraceSessionManager::Dispatch(...) -> bool`：现在会显式告诉上层“这次 ready trace 是否真的成功投进线程池”。
- `PushResult::AcceptedDeferred`：表示当前 span 已经被服务端接收，但 ready trace 因 submit 失败暂时留在服务端内部延后重试。
- `ScheduleRetryNode(...)`：为 `ReadyRetryLater` 会话安排一个更短的重试时点，避免 submit 失败后立刻原地再撞线程池。

Pitfalls:
- 如果先把 `unique_ptr<TraceSession>` 直接 move 成 owning `shared_ptr` 再调用 `submit`，一旦 submit 失败就很难无损拿回所有权做回滚。
- 当前最小实现虽然已经区分了 `ReadyRetryLater` 语义，但底层仍借用了同一套时间轮基础设施；后续若重试策略更复杂，再考虑独立重试容器会更清晰。
