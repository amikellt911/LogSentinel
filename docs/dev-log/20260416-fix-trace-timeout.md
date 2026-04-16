# 2026-04-16 fix(trace): 修正 collecting timeout 精确截止

## Git Commit Message

`fix(trace): 修正 collecting timeout 提前触发`

## Modification

- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `server/tests/benchmark/suite_b/README.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- `TraceSession` 增加 `collect_deadline_ms`，把 `Collecting` 阶段的真实毫秒截止时间和时间轮 tick 分开。
- `PushLocked` 在 collecting 阶段每收到新 span 都刷新精确 deadline。
- `SweepExpiredSessions` 在 dispatch 前二次检查 `now_ms >= collect_deadline_ms`，避免时间轮粗 tick 提前命中槽位后过早摘走 session。
- `RebuildTimeWheel` 在 idle timeout 运行时变更时同步重算 collecting deadline，避免时间轮和精确 deadline 使用两套配置。
- README、benchmark 总览和 Suite B 文档补充生命周期口径：`idle timeout` 不会先进 `sealed grace`，它只是 collecting 收集等待截止。

## Verification

- `cmake --build server/build --target test_trace_session_manager_unit -j2`
- `./server/build/test_trace_session_manager_unit`
- `./server/build/test_trace_session_manager_integration --gtest_filter='TraceSessionManagerIntegrationTest.DispatchesOnIdleTimeoutWithoutTraceEnd:TraceSessionManagerIntegrationTest.FrequentUpdatesPreventEarlyTimeoutDispatch'`

## Learning Tips

### Newbie Tips

- 时间轮适合做“粗唤醒”，不适合独自表达毫秒级业务 deadline。既然 tick 会向上取整、第一次 sweep 还会推进一个 tick，那么真正摘走 session 前必须再看业务时间戳。
- 单测里如果手动传 `SweepExpiredSessions(now_ms)`，就不能用真实时钟版 `Push()` 混着测。否则 span 到达时间来自 steady clock，而 sweep 时间来自 fake timeline，测试结果会漂。

### Function Explanation

- `PushLocked(span, now_ms)`：测试可控入口，用固定 `now_ms` 模拟 span 到达时间，避免真实时钟影响 timeout 断言。
- `ComputeDelayTicks(ms)`：把毫秒延迟换算成时间轮 tick，使用向上取整保证不会早于目标时间唤醒。
- `RebuildTimeWheel()`：配置变化后清空旧时间轮节点，再按当前 session 生命周期重新挂节点。

### Pitfalls

- 不能把 `Collecting timeout` 理解成 `Collecting -> Sealed -> Dispatch`。当前产品语义是：没有明确封口信号时，collecting 等满 idle timeout 后直接准备走统一 dispatch 主路径。
- `Sealed` 只由 `trace_end / capacity / token_limit / duplicate_span` 等明确封口条件触发；sealed deadline 不会因为 late span 续命。
- 改测试后必须重新编译测试二进制。直接跑旧的 `server/build/test_*` 会得到旧源码行为，容易误判修复没生效。
