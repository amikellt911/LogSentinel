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
