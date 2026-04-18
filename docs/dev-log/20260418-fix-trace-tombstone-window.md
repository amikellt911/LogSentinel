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
