# 20260316 fix trace-primary-flush-logging

Git Commit Message
fix(persistence): 补充主数据批量落库失败日志

Modification
- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_Benchmark.md
- docs/dev-log/20260316-fix-trace-primary-flush-logging.md

Learning Tips
Newbie Tips
- 批量写库失败时，如果代码只返回一个 `false`，那你后面看到的就只剩“失败次数”，根本不知道是哪一批、哪段 trace_id、也不知道具体 SQLite 报了什么错。排障阶段最值钱的不是多一个计数器，而是第一时间把失败上下文打出来。
- `summary_count/span_count + first_trace_id/last_trace_id` 这种边界信息很实用。既然一批数据已经被聚合成 batch 了，那日志也应该按 batch 维度给线索，不然你只能在几千条 trace 里盲猜。

Function Explanation
- `SqliteTraceRepository::SavePrimaryBatch`
  这次没改事务语义，也没改失败处理策略，只在 `catch` 里补了一条错误日志。既然真正抛异常的是 `checkSqliteError(...)`，那就直接把 `e.what()` 打出来，再把这批数据的 summary/span 数量和 trace_id 边界一起带上，方便下一轮 benchmark 后直接从 `server-w*.log` 里定位是哪种 SQLite 错。

Pitfalls
- 不要一上来就加重试。既然现在连失败原因都不知道，盲目重试只会把“主键冲突/约束错误”伪装成“偶发抖动”，最后越改越乱。
- 不要把日志只打在缓冲层。`BufferedTraceRepository` 只知道 `saved=false`，真正知道 SQLite 为什么失败的是 `SqliteTraceRepository`；日志必须落在离异常最近的地方。

追加验证
- `cmake --build server/build --target LogSentinel -j2` 已通过。

追加记录
Git Commit Message
feat(core): 为 Trace sealed 和 tombstone 补状态骨架

Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260316-fix-trace-primary-flush-logging.md

Learning Tips
Newbie Tips
- 这种“先搭状态骨架、后改行为逻辑”的分步法很有必要。既然后面要同时改 `PushLocked / DispatchLocked / SweepExpiredSessions`，那你如果一上来边加字段边改流转，很容易把 bug 全搅在一起，最后连编译不过和逻辑错都分不清。
- `tombstone` 在这里不是缓存结果，也不是历史全局去重表。它只是“最近刚完成过的 trace_key 集合”，职责是延迟遗忘，防止晚到 span 把旧 trace 复活。

Function Explanation
- `TraceSession::SealReason / LifecycleState::Sealed`
  这一步先把 sealed 的状态语义放进 `TraceSession`。既然 `trace_end/capacity/token_limit` 后面都要统一改成“先封口、再给短窗口、最后再 dispatch”，那状态本身必须先有地方落。
- `completed_trace_expire_tick_ / completed_trace_wheel_`
  这里先把 completed tombstone 的两套基础结构放进 manager。既然我们已经决定不把 tombstone 和活跃 session 混成一种时间轮节点，那么就先把“精确查找 map + 按 tick 过期回收桶”这套骨架补齐，后面行为改造时才不会再反复改成员布局。

Pitfalls
- 不要把 `CompletedTombstone` 也塞进 `TraceSession::LifecycleState`。既然 dispatch 成功后 session 本体就应该离开活跃表，那 completed 这段短期保留状态应该放在 manager 外层结构里管，而不是继续占着一个活跃 session。
- 不要现在就急着改调度逻辑。既然这一步的目标只是补字段和初始化，那么行为保持不变反而最稳；这样后面一旦出问题，你就知道是第二步、第三步的新逻辑炸了，不是第一步骨架导致的。

追加验证
- `cmake --build server/build --target LogSentinel -j2` 已通过。
