Git Commit Message:
test(core): 修正trace异步分发后的回滚单测口径

Modification:
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:

Newbie Tips:
- 把同步逻辑改成异步后，最容易先炸掉的不是业务代码本身，而是测试里的“时序假设”。以前 `SweepExpiredSessions()` 返回时 session 已经同步回滚，现在改成独立 dispatch 线程后，sweep 返回只代表 job 已经投出去，不代表回滚已经完成。
- 这类测试不要写固定 `sleep`，而要等“状态真正出现”。因为线程调度和机器负载都会变，固定睡眠只会让单测偶现抖动。

Function Explanation:
- `WaitUntil(...)`：这里用短轮询包住异步条件，作用是把“线程什么时候跑到”转换成“状态什么时候满足”。只要条件函数写得够具体，测试就会比硬编码等待时间稳定很多。
- `std::function<bool(const TraceSession&)>`：这里把每条用例真正关心的状态判断抽成 predicate，避免把“等待 session 出现”和“断言 session 内容”重复写 7 遍。

Pitfalls:
- 不能只等 `submit_fail_count` 这类原子计数。因为 dispatch 线程里是先 `submit_fail_count++`，后面才拿 manager 锁做 `RestoreSessionLocked()`；如果测试只看计数，就会在“计数变了但 session 还没塞回去”的时间缝里误判失败。

---

追加记录（2026-04-03，prepared 读取改成按需分支 + 指针视图）：

Git Commit Message:
refactor(core): 收口dispatch阶段prepared缓存的按需读取

Modification:
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:

Newbie Tips:
- 如果 prepared payload / summary 已经存在，先把它们当“只读视图”用起来，比先拷到局部变量里再往后传更稳。因为只读视图不改变所有权，submit 失败时 session 还能完整带着缓存回滚。
- 把 `need_payload || need_summary || need_primary` 这种大 if 拆成三个按需分支后，读代码时就不会一直在脑子里来回切换“这轮到底是补 payload，还是补 summary，还是补 primary”。

Function Explanation:
- `const T*` 在这里不是为了搞复杂，而是为了表达“当前只是借用 session 里已经存在的数据，不拥有它”。真正跨线程时，再配合 `session_holder` 保证这些指针指向的数据在 worker 期间一直活着。
- `std::optional<TraceIndex>`：这里用来做惰性构建。既然有些分支根本不需要建树，就不要一上来把 CPU 重活先做了。

Pitfalls:
- 这里能用指针视图，是因为 worker lambda 同时持有 `session_holder`。既然 session 本体在 lambda 生命周期内不会释放，那么指向 `prepared_trace_payload / prepared_summary` 的指针才安全；如果后面有人把 `session_holder` 从 lambda 里删了，这里会立刻变成悬空指针风险点。

---

追加记录（2026-04-03，补 primary append 回滚注释）：

Git Commit Message:
docs(core): 补充trace主数据append与回滚路径注释

Modification:
- server/core/TraceSessionManager.cpp

Learning Tips:

Newbie Tips:
- `primary_enqueued=true` 只表示主数据已经成功 append 到 BufferedTraceRepository，不表示 SQLite 已经 flush，也不表示 AI analysis 已经完成。这三个阶段不能混成一个“成功”状态。

Function Explanation:
- `AppendPrimary(...)`：这里不是直接落 SQLite，而是把 trace 的 summary + spans 先送进缓冲写入器。后面真正的 batch flush 由 BufferedTraceRepository 自己的后台线程处理。

Pitfalls:
- 如果 `AppendPrimary` 失败时只返回，不同步删 inflight 并恢复 session，那么这条 trace 就会卡在“manager 里查不到、tombstone 也没有”的中间态，后面晚到 span 会把状态机撕裂。
