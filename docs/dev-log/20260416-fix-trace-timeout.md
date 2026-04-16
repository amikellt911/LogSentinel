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

---

# 2026-04-16 fix(benchmark): 修正 Suite B 真值与 duplicate 归因口径

## Git Commit Message

`fix(benchmark): 修正 Suite B 真值与 duplicate 归因口径`

## Modification

- `server/tests/benchmark/suite_b/sender.py`
- `server/tests/benchmark/suite_b/evaluator.py`
- `server/tests/benchmark/suite_b/sender_unit_test.py`
- `server/tests/benchmark/suite_b/evaluator_unit_test.py`
- `server/tests/benchmark/suite_b/README.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- sender 新增 per-trace 真值收口：先找到有效 tail/trace_end 的计划到达时间，再按 protected sealed grace 窗口重写 `late_after_dispatch` 的 `expected_final_action`。
- evaluator 新增逐 span 持久化计数，`duplicate_persistence_rate` 改成按 replay clone 自身 `span_id` 的持久化次数判断，不再被同 trace 其它 extra span 连坐。
- Suite B README 更新指标说明，明确 manifest 真值不是静态 delay bucket 标签。

## Verification

- `python3 -m unittest sender_unit_test.py`
- `python3 -m unittest evaluator_unit_test.py`
- `python3 -m unittest discover -s . -p '*_unit_test.py'`
- `python3 -m py_compile run_suite_b_matrix.py sender.py evaluator.py run_suite_b.py profiles.py`
- `python3 server/tests/benchmark/suite_b/run_suite_b_matrix.py --server-bin ./server/build/LogSentinel --run-root /tmp/suite_b_matrix_truth_fix_v1 --port-base 19580 --server-cpuset 1-3 --server-io-threads 2 --worker-threads 8 --dispatch-worker-threads 2 --worker-queue-size 4096 --disable-ai --disable-webhook --trace-count 10 --spans-per-trace 8 --send-workers 2 --trace-idle-timeout-ms 800 --trace-sweep-interval-ms 100 --trace-max-dispatch-per-tick 64 --trace-buffered-span-limit 4096 --trace-active-session-limit 512 --sender-profiles clean_baseline,mixed_realistic,late_replay_stress --trace-lifecycle-profiles protected,minimal`

## Learning Tips

### Newbie Tips

- benchmark 的 manifest 是“真值账本”，不能只记录发送动作，还要记录这个动作在目标生命周期语义下应该产生什么结果。
- 指标归因要避免连坐。pollution 是 extra span 问题，duplicate 是 replay clone 自己是否重复持久化的问题，两个指标不能混在一起。

### Function Explanation

- `finalize_expected_actions_for_trace()`：按同一 trace 的有效 tail 到达时间和 grace 窗口，统一修正事件级 expected action。
- `persisted_span_counts`：SQLite 快照中的逐 trace/逐 span 行数计数，用来判断同一 span_id 是否重复落库。

### Pitfalls

- `late_after_dispatch` 只是抽样桶名，不等于真实生命周期已经 dispatch；如果 trace_end 自己晚到，这个 span 仍可能处于 sealed grace 内。
- replay clone 复制的是已有 span_id，单纯看 `set(span_id)` 看不出重复持久化，必须保留计数。

---

# 2026-04-16 feat(benchmark): 补 Suite B p95 性能护栏

## Git Commit Message

`feat(benchmark): 补 Suite B p95 性能护栏`

## Modification

- `server/tests/benchmark/suite_b/run_suite_b.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_unit_test.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`
- `server/tests/benchmark/suite_b/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 单次 Suite B case 现在会从 `manifest.jsonl` 的 `actual_send_start_ms / actual_send_done_ms` 计算 `ingest_latency_ms`。
- `ingest_latency_ms` 输出 `count / min / max / avg / p50 / p95 / p99`，口径只覆盖 `/logs/spans` HTTP 请求本身。
- Suite B matrix summary 新增 `ingest_p95_latency_delta_by_profile`，按 sender profile 汇总 `protected - minimal` 的 p95 绝对增量和相对增量。
- 文档补充说明：p95 护栏不包含 evaluator 等待 SQLite 稳定、结果查询或后端进程起停时间，避免把后台 drain 成本混进入口延迟。

## Verification

- `python3 -m unittest run_suite_b_unit_test.py run_suite_b_matrix_unit_test.py`
- `python3 -m unittest discover -s . -p '*_unit_test.py'`
- `python3 -m py_compile run_suite_b_matrix.py sender.py evaluator.py run_suite_b.py profiles.py sender_unit_test.py evaluator_unit_test.py run_suite_b_unit_test.py run_suite_b_matrix_unit_test.py`

## Learning Tips

### Newbie Tips

- benchmark 指标要先定义“时间窗口”。入口 HTTP p95、SQLite flush 耗时、evaluator drain 等待是三种不同时间，混在一起会让结论不可解释。
- percentile 计算要固定口径。本次使用 nearest-rank，样本少时 p95 往往等于最大值，这是正常现象，不是算法坏了。

### Function Explanation

- `load_ingest_latency_stats()`：读取 sender manifest，把每条事件的 `actual_send_done_ms - actual_send_start_ms` 折叠成延迟统计。
- `build_ingest_p95_latency_delta_by_profile()`：按 sender profile 配对 `protected/minimal` case，计算 p95 的绝对和相对增量。

### Pitfalls

- `--send-workers` 是 sender 内部并发数，不是 CPU 绑核；如果不配合外层 `taskset`，p95 结果仍可能被 sender 和后端抢核污染。
- dry-run 或 fake case 可能出现 `minimal_p95 = 0`，这时相对增量不能硬除，结果里保留为 `null`。
