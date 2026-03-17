# 20260312 fix trace-runtime-stats

Git Commit Message
fix(perf): 修正 Trace 双缓冲阶段的运行时埋点口径

Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/persistence/BufferedTraceRepository.h
- server/persistence/BufferedTraceRepository.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- docs/PERFORMANCE_PHASE_TEMPLATE.md
- docs/todo-list/Todo_Benchmark.md
- docs/dev-log/20260312-fix-trace-runtime-stats.md

Learning Tips
Newbie Tips
- 双缓冲一旦把真实写库动作挪到后台 flush 线程，原来在前台 worker 里量到的时间就不再是“SQLite 写入时间”。既然时间线已经换了，埋点口径也必须跟着换；不然数字看起来很漂亮，结论却是错的。
- 性能埋点最怕“字段名没改，语义已经变了”。这次如果继续保留 `save_calls/save_total_ns` 这种名字，后面看日志的人很自然会把它当成真实持久化成本，最后论文里就会把 enqueue 时间误写成 SQLite 时间。

Function Explanation
- `TraceSessionManager::DescribeRuntimeStats`
  这里把原来的 `save_*` 明确改成 `analysis_enqueue_*`。既然 manager 现在只负责把 analysis 塞进缓冲写入器，那它就只能诚实地说“我量的是 enqueue”，不能再冒充真实落库。
- `BufferedTraceRepository::DescribeRuntimeStats`
  这次新增了一组后台 flush 线程专属埋点。既然真正慢的是 `SavePrimaryBatch / SaveAnalysisBatch`，那就必须在 flush 线程里围住这两个调用量耗时、次数、失败数和刷出的行数，最后在析构 drain 完之后统一打印。

Pitfalls
- 不要在 manager 析构里试图伪装成“最终 SQLite 统计”。既然 `BufferedTraceRepository` 的析构发生在 manager 之后，那么最后半桶的 drain 只有缓冲层自己知道；如果把最终写库统计绑死在 manager 析构里，最后一批永远会漏账。
- 不要只补代码，不改实验模板。既然论文记录表和 benchmark 解读口径都还写着 `save_calls/save_total_ns`，那你哪怕代码改对了，后面抄表的人还是会按旧字段误读。

追加验证
- `cd server && cmake --build build --target test_trace_session_manager_unit -j2` 已通过。
- `./server/build/test_trace_session_manager_unit --gtest_filter=TraceSessionManagerUnitTest.RuntimeStatsTrackDispatchWorkerAiEnqueueAndBufferedFlush:TraceSessionManagerUnitTest.PushReturnsAcceptedDeferredAndRollsBackWhenSubmitFails` 已通过。

追加记录
Git Commit Message
fix(benchmark): 输出 benchmark 阶段的运行时埋点摘要

Modification
- server/tests/wrk/run_bench.sh
- docs/todo-list/Todo_Benchmark.md
- docs/dev-log/20260312-fix-trace-runtime-stats.md

Learning Tips
Newbie Tips
- benchmark 脚本如果只管“起服务、跑 wrk、停服务”，最后你还是得手翻 `server.log` 才能抄埋点，这种流程很容易把数据抄错。既然 runtime stats 本来就在服务退出时打印，那脚本就应该顺手把最后一条摘出来，直接写进 `run-summary.log`。

Function Explanation
- `emit_runtime_stats`
  这一步加了一个很小的汇总函数：每轮 worker 配置跑完、服务 clean shutdown 之后，直接从对应 `server-w*.log` 里抓最后一条 `[TraceRuntimeStats]` 和 `[BufferedTraceRuntimeStats]`，再 `tee -a` 到 `run-summary.log`。既然服务已经退出，最后统计行也已经落盘，这时候摘数据最稳。

Pitfalls
- 不要在服务还活着的时候提前 grep runtime stats。既然这两行是在析构阶段打印的，那你如果在 cleanup 前就去抓，八成只能拿到空结果，然后误以为埋点没生效。

追加验证
- `bash -n server/tests/wrk/run_bench.sh` 已通过。

追加记录
Git Commit Message
fix(benchmark): 补齐 run_bench 的停机埋点输出

Modification
- server/src/main.cpp
- server/tests/wrk/run_bench.sh
- docs/PERFORMANCE_PHASE_TEMPLATE.md
- docs/todo-list/Todo_Benchmark.md
- docs/dev-log/20260312-fix-trace-runtime-stats.md

Learning Tips
Newbie Tips
- benchmark 结束时最容易踩的坑，不是“压不起来”，而是“停不下来”。既然 worker 线程池默认会把队列里的任务全做完再退出，那么一轮压测打出几百上千个待分析 trace 后，`SIGTERM` 不等于立刻退出，脚本就会卡在 `wait` 上。
- 析构阶段埋点如果只靠“最后正常退出时打印”，那一旦退出过程太慢或者被强杀，你拿到的就不是“没数据”，而是“数据其实打了但脚本没等到”。所以 benchmark 场景最好补一个 shutdown snapshot，在收到停机信号时先把当前账本打印出来。

Function Explanation
- `HandleProcessSignal + loop.runEvery(0.1, ...)`
  这里把信号处理拆成两段：signal handler 只置位，EventLoop 定时检查后再打印 runtime stats 并 `quit()`。既然信号上下文里不能乱做复杂逻辑，那么真正的日志输出和对象收尾必须放回正常线程里做。
- `wait_for_pid_exit`
  这一步给 `run_bench.sh` 的 cleanup 加了上限等待。既然 benchmark 主要目标是拿数据，不是无限陪后台排队做完所有 AI 任务，那么超过阈值就保留已打印的 snapshot，再用 `SIGKILL` 收尾，避免整轮实验卡死。

Pitfalls
- 不要把 `TraceSessionManager` 的定时器回调继续绑成 `shared_ptr` 捕获。既然 TimerQueue 会一直持有这个回调直到 EventLoop 析构，那 manager 的真实析构时机就会被拖到线程池和 notifier 后面，退出顺序马上乱掉。
- 不要把 `BufferedTraceRuntimeStats` 误读成“最终全量落库总数”。现在 `run_bench` 输出的是 benchmark 停止时刻的快照；如果脚本随后因为积压过深强制 kill 进程，那么快照之后那部分 backlog 本来就不属于这轮已完成的结果。

追加验证
- `cmake --build server/build --target LogSentinel -j2` 已通过。
- `bash -n server/tests/wrk/run_bench.sh` 已通过。
- `DURATION=1s CONNECTION_SET="1" WORKER_THREAD_SET="1" WRK_THREADS=1 STOP_RETRY=25 STOP_SLEEP_SEC=0.2 bash server/tests/wrk/run_bench.sh end` 已验证会把 `[TraceRuntimeStats]` 和 `[BufferedTraceRuntimeStats]` 写入 `run-summary.log`。
