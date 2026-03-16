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
feat(core): 接入 Trace sealed 短窗口并收紧 retry_later 语义

Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260316-fix-trace-primary-flush-logging.md

Learning Tips
Newbie Tips
- `trace_end` 不等于“这个 trace 以后绝对不会再来 span”。真实网络里最后一个包完全可能先到，所以如果一看到 `trace_end` 就立刻 dispatch，你其实是在赌网络时序。
- `sealed` 和 `ready_retry_later` 不是一回事。前者还是“短暂收集态”，允许晚到 span 并进来；后者已经是“重投态”，主数据可能已经 append 进缓冲区了，再并入新 span 只会把内存版本和已落库版本撕裂。

Function Explanation
- `ComputeSealDelayTicks`
  这里把三种封口原因拆成两档：`trace_end` 给 2 tick，`capacity/token_limit` 给 1 tick。既然显式结束更像“最后一个包也许先到”，那就多给一点乱序窗口；而容量/token 打满本质是保护性截断，就不要再拖。
- `SealSessionLocked`
  这一步只做三件事：把生命周期切到 `Sealed`、写死 `sealed_deadline_tick`、把节点重新挂回时间轮。重点不是“重新开始等”，而是“从现在起只等到这个固定 deadline 为止”。

Pitfalls
- sealed 会话如果每来一个 late span 就把 deadline 往后推，看起来像“更完整”，实际上会把状态机抖坏。只要上游有持续乱序，这条 trace 就可能永远不 dispatch。
- `ReadyRetryLater` 如果还继续吃新 span，下一次重试时 AI 看到的 payload 会比第一次 append primary 时更大，结果就是 `trace_summary/trace_span` 和 `trace_analysis` 描述的根本不是同一份 trace。

追加验证
- 计划执行 `cmake --build server/build --target test_trace_session_manager_unit -j2` 与 sealed/retry_later 相关最小用例回归。

追加记录
Git Commit Message
feat(core): 为已完成 trace 接入 TIME_WAIT tombstone

Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260316-fix-trace-primary-flush-logging.md

Learning Tips
Newbie Tips
- `tombstone` 不是缓存历史结果，也不是查数据库做全局去重。它只是“这几秒内刚完成过的 trace_key 集合”，职责非常窄：拦截晚到包，防止旧 trace 被复活。
- 时间轮里按 `% wheel_size` 取槽只是粗筛，不是精确到期。既然 completed tombstone 也会回环，那么 sweep 时同样必须回到 `completed_trace_expire_tick_` 看真实过期 tick，不能看到当前槽就直接删。

Function Explanation
- `AddCompletedTombstoneLocked`
  这一步在 `thread_pool_->submit(...)` 成功之后调用。既然只有真正成功进入 worker 队列，才说明这条 trace 已经完成并进入下游，那么 tombstone 也只能在这里写，不能提前。
- `IsCompletedTombstoneAliveLocked`
  Push 新 span 时先查这个函数。既然活跃 session 不存在，但这个 key 还处在 TIME_WAIT，那么这就是晚到 span，不该新建 session；如果发现 map 里只是过期脏数据，这里会顺手清掉。
- `SweepCompletedTombstonesLocked`
  这一步专门扫独立的 completed wheel。既然 tombstone 过期和 live session dispatch 是两套完全不同的语义，那么拆开管理比把各种 type 塞进一套节点里更容易保证状态不串。

Pitfalls
- `SweepExpiredSessions` 之前一上来如果发现 `sessions_.empty()` 就直接 return，那么 tombstone 永远不会过期。因为 trace 正常完成后，活跃 session 本来就应该清空，completed wheel 只能靠 sweep 自己继续推进。
- 不要把 tombstone 写在 `AppendPrimary` 成功后。那时只说明主数据进了缓冲区，不说明这条 trace 已经成功进入 worker 队列；如果随后 submit 失败，它还会回滚成 `ReadyRetryLater`，这时提前写 tombstone 会把活 trace 误判成“已完成”。

追加验证
- 计划执行 `cmake --build server/build --target test_trace_session_manager_unit -j2`。
- 计划执行 tombstone 相关最小单测回归。
- 已执行 `cd server && cmake --build build --target test_trace_session_manager_unit -j2`，通过。
- 已执行 `cd server && ./build/test_trace_session_manager_unit`，32/32 通过。

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
