# 2026-04-18 fix(core): 修正 Trace tombstone 窗口被 sweep tick 压短

## Git Commit Message

`fix(core): 修正 Trace tombstone 窗口被 sweep tick 压短`

## Modification

- `server/core/TraceSessionManager.cpp`
- `server/core/TraceSessionManager.h`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 修正 completed tombstone 的时间语义：从固定 `25 tick` 改为固定 `12500ms` 后按当前 `wheel_tick_ms` 向上换算。
- 这次云机 Suite D 暴露的问题是：benchmark 为了更快 dispatch 把 `trace_sweep_interval_ms` 压到 `20ms`，但 tombstone 仍写死 `25 tick`，TIME_WAIT 从 `12.5s` 退化成 `500ms`。
- TIME_WAIT 过短时，同一 trace 的慢到 span 会在 tombstone 过期后复活旧 trace_key，后续再次写入 `trace_summary`，触发 SQLite `UNIQUE constraint failed: trace_summary.trace_id`。
- 修复后：
  - `sweep=500ms` 仍得到 `25 tick`，保持旧默认窗口；
  - `sweep=20ms` 会得到约 `625 tick`，保持同样的 `12.5s` 防复活窗口；
  - dispatch 频率和 tombstone 保护时长不再互相误伤。
- 新增单测 `CompletedTombstoneWindowDoesNotShrinkWithFastSweepTick`，复现 `wheel_tick_ms=20` 时 600ms 后 tombstone 仍应存在，并且 late span 不能复活新 session。

## Verification

- 先写红灯测试并确认旧代码失败：
  - `./server/build-main/test_trace_session_manager_unit --gtest_filter=TraceSessionManagerUnitTest.CompletedTombstoneWindowDoesNotShrinkWithFastSweepTick`
  - 失败点：旧代码只剩 `25 tick`，600ms 后 tombstone 过期并创建新 session。
- `cmake --build server/build-main --target test_trace_session_manager_unit`
- `./server/build-main/test_trace_session_manager_unit --gtest_filter=TraceSessionManagerUnitTest.CompletedTombstoneWindowDoesNotShrinkWithFastSweepTick`
- `./server/build-main/test_trace_session_manager_unit`
- `./server/build-main/test_trace_session_manager_integration`
- `python3 -m unittest server.tests.benchmark.suite_d.trace_model_suite_d_unit_test server.tests.benchmark.suite_d.run_suite_d_case_unit_test server.tests.benchmark.suite_d.suite_d_frozen_wrappers_unit_test`
- `bash -n server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh server/tests/benchmark/run_paper_benchmark_cloud.sh`
- `cmake --build server/build-main --target LogSentinel`
- `git diff --check`

## Learning Tips

### Newbie Tips

- tick 是调度粒度，不是业务时间语义。业务上要保留 `12.5s` 的 TIME_WAIT，就不能直接写死 `25 tick`，因为 tick 可能从 `500ms` 被调到 `20ms`。
- 性能调参最容易误伤“保护窗口”。看起来只是把 sweep 调快，实际却同时缩短了 tombstone 生命周期，最后表现成数据库 UNIQUE 冲突。

### Function Explanation

- `ComputeDelayTicks(delay_ms)`：把毫秒窗口按当前 `wheel_tick_ms_` 向上换算成 tick，并至少返回 `1`。
- `AddCompletedTombstoneLocked()`：trace 完成后写入 TIME_WAIT tombstone，用来拦截短时间内同 trace_key 的 late span。
- `SweepCompletedTombstonesLocked()`：按 `expire_tick` 回收 tombstone；如果当前槽里扫到未来才过期的 tombstone，会重新挂回目标槽。

### Pitfalls

- 不要用 `sweep=500ms` 作为最终性能参数。它只能验证 tombstone 根因，因为它会明显拖慢 dispatch。
- 修复后正式 Suite D 仍应回到 `sweep=20ms + max_dispatch_per_tick=256` 这类高频 dispatch 参数，再观察 `primary_flush_fail_count` 是否下降。

---

## 追加记录：fix(benchmark): 修正 Suite D final 口径被停服时序低估

### Git Commit Message

`fix(benchmark): 修正 Suite D final 口径被停服时序低估`

### Modification

- `server/tests/benchmark/suite_d/run_suite_d_case.py`
- `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

### Summary

- 修正 Suite D single-case runner 的 final 统计时序：
  - 之前是在服务进程还活着时先读 SQLite `final`，然后才停服；
  - 如果 buffered repo 或 shutdown drain 还会继续补写，benchmark 就会把这段尾巴提前漏掉。
- 现在改成：
  - `sqlite_counts_at_stop` 继续保留 wrk 停止时的在线完成量；
  - 先停服，确保 buffered repo / dispatch / shutdown 路径都执行完；
  - 再等待 SQLite 计数稳定，并把它写入 `sqlite_counts_final`。
- 同时去掉 Suite D final 等待对 `offered_traces` 的强制追满：
  - Suite D 存在 `503/non-2xx` 时，`offered_traces` 会高于“真正被服务端接受的 trace 数”；
  - 如果继续拿它当 `expected_trace_count`，`drain_tail_ms` 会被误报成永远超时。
- 新增单测 `test_run_suite_d_case_stops_server_before_waiting_for_final_sqlite_counts`，锁住“先停服，再等 final”的关键时序。

### Verification

- 先写红灯测试并确认旧代码失败：
  - `python3 -m unittest server.tests.benchmark.suite_d.run_suite_d_case_unit_test.SuiteDRunSuiteDCaseUnitTest.test_run_suite_d_case_stops_server_before_waiting_for_final_sqlite_counts`
  - 失败点：旧逻辑会先调 `wait_until_sqlite_stable`，`stop_server_process` 还没发生。
- `python3 -m unittest server.tests.benchmark.suite_d.trace_model_suite_d_unit_test server.tests.benchmark.suite_d.run_suite_d_case_unit_test server.tests.benchmark.suite_d.suite_d_frozen_wrappers_unit_test`
- `bash -n server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh server/tests/benchmark/run_paper_benchmark_cloud.sh`
- `git diff --check`

### Learning Tips

#### Newbie Tips

- benchmark 里的 `online / stop / final` 是三个不同口径：
  - `online` 看测量窗口内已经落库多少；
  - `stop` 看 wrk 停止瞬间 SQLite 已经看见多少；
  - `final` 看整条 case 完全收尾后最终落了多少。
- 如果把 `final` 提前到停服前读取，含缓冲写入的系统很容易被测成“少落了一截”。

#### Function Explanation

- `stop_server_process(...)`：结束 auto-start 的后端进程，并等待它把退出路径走完。
- `wait_until_sqlite_stable(...)`：轮询 SQLite 计数，直到计数稳定或超时。
- `sqlite_counts_at_stop / sqlite_counts_final`：前者服务在线指标，后者服务 case 完整收尾口径，不能混用。

#### Pitfalls

- `offered_traces` 只是 wrk 侧“理论上发起了多少条 trace”，不等于后端一定接受了这么多 trace。
- 只要压测里存在 `503/non-2xx`，就不能再拿 `offered_traces` 当 final drain 的硬目标，否则 `drain_tail_ms` 会被系统性高估。
