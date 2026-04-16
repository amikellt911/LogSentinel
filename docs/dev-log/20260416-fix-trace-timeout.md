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

---

# 2026-04-16 fix(benchmark): 让 Suite B run-root 自动带时间后缀

## Git Commit Message

`fix(benchmark): 让 Suite B run-root 自动带时间后缀`

## Modification

- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`
- `server/tests/benchmark/suite_b/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- `run_suite_b_matrix.py` 的 `--run-root` 现在表示实验目录前缀，而不是最终写入目录。
- 每次运行都会自动把前缀解析成 `prefix-YYYYMMDD-HHMMSS-mmmms` 形式的真实目录，避免复跑同一条命令时复用旧 SQLite、manifest 和 result 资产。
- matrix summary 新增 `requested_run_root / actual_run_root`，方便回溯“命令里写的是什么前缀”和“本轮结果真正落到哪里”。
- README 和 benchmark 总览文档同步说明：目录区分统一靠时间后缀，不再建议手写 `v1/v2`。

## Verification

- `python3 -m unittest run_suite_b_matrix_unit_test.py`
- `python3 -m unittest discover -s . -p '*_unit_test.py'`
- `python3 -m py_compile run_suite_b_matrix.py sender.py evaluator.py run_suite_b.py profiles.py sender_unit_test.py evaluator_unit_test.py run_suite_b_unit_test.py run_suite_b_matrix_unit_test.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 资产目录如果允许复用旧 SQLite，最先被污染的往往不是脚本本身，而是你后面看到的“指标很怪”。所以 run-root 最好从一开始就设计成“一次运行一个实际目录”。
- “用户输入前缀”和“真实落盘路径”是两个概念。把它们分开记录，比靠人工在 shell 里手写 `v1/v2` 更稳，也更方便论文复现实验。

### Function Explanation

- `format_run_timestamp()`：把本地时间格式化成 `YYYYMMDD-HHMMSS-mmmms` 形式，用作 benchmark 目录的时间后缀。
- `resolve_run_root()`：接收实验前缀，返回 `requested_run_root / actual_run_root`。
- `ensure_run_root_resolved()`：在 matrix runner 真正建目录前完成 prefix -> actual 解析，并把结果写回 `args`。

### Pitfalls

- 不能直接拿用户传进来的 `--run-root` 当真实目录用。只要这条命令被重复执行一次，旧 SQLite 就会和新 case 混在一起。
- 如果 summary 只记 actual 目录，不记 requested 前缀，后面回看命令和资产时很容易对不上。

---

# 2026-04-16 feat(benchmark): 补 Suite B SQLite UNIQUE 冲突诊断指标

## Git Commit Message

`feat(benchmark): 补 Suite B SQLite UNIQUE 冲突诊断指标`

## Modification

- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`
- `server/tests/benchmark/suite_b/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- `run_suite_b_matrix.py` 现在会在每个 case 停机后读取对应 `server.log`，统计 `UNIQUE constraint failed: trace_summary.trace_id` 的出现次数，并写入 `sqlite_unique_constraint_fail_count`。
- matrix summary 会额外输出 `sqlite_unique_constraint_fail_count_by_case`，方便一眼看出哪些 case 已经出现重复 summary 写尝试。
- 这个指标是日志诊断证据，不等于最终真的重复持久化成功了多少条。也就是说，即使 `duplicate_persistence_rate = 0`，后端仍可能已经撞过 UNIQUE 约束，只是 SQLite 把脏写挡住了。
- 本地 4 核验证里，这条指标正好能补足 `minimal` 脏时序 case 的解释：主指标只看最终结果，日志指标则揭示“后端内部已经发生过重复提交风险”。

## Verification

- `cd server/tests/benchmark/suite_b && python3 -m unittest discover -s . -p '*_unit_test.py'`
- `cd server/tests/benchmark/suite_b && python3 -m py_compile run_suite_b_matrix.py sender.py evaluator.py run_suite_b.py profiles.py sender_unit_test.py evaluator_unit_test.py run_suite_b_unit_test.py run_suite_b_matrix_unit_test.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 结果最好分成“主指标”和“诊断指标”两层。主指标回答论文结论，诊断指标负责解释为什么会这样，别把两种口径混成一个数字。
- 数据库 UNIQUE 冲突不等于系统没问题，它只说明“最终脏数据没落进去”。如果日志里持续撞约束，说明上游生命周期或去重语义已经开始打架了。

### Function Explanation

- `count_sqlite_unique_constraint_failures()`：读取 case 对应的 `server.log`，只统计 `trace_summary.trace_id` 的 UNIQUE 冲突次数。
- `build_sqlite_unique_constraint_fail_count_by_case()`：把各 case 的冲突次数整理成 summary 级映射，方便做矩阵对比。

### Pitfalls

- 不能在停机前就去读 `server.log`。因为最后一波 flush 或错误日志可能还没刷完，太早统计会少算。
- 不能把 `sqlite_unique_constraint_fail_count` 当成 `duplicate_persistence_rate` 的替代品。一个是“尝试过脏写几次”，一个是“最终真的重复持久化了多少次”，语义不是一回事。

---

# 2026-04-16 feat(benchmark): 固定 Suite B 正式 5 seed 复跑口径

## Git Commit Message

`feat(benchmark): 固定 Suite B 正式 5 seed 复跑口径`

## Modification

- `server/tests/benchmark/suite_b/run_suite_b_campaign.py`
- `server/tests/benchmark/suite_b/run_suite_b_campaign_unit_test.py`
- `server/tests/benchmark/suite_b/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- 新增 `run_suite_b_campaign.py`，把正式 Suite B 结果从“单次 matrix”提升为“5 个固定 seed 的 campaign”。
- 默认 seed 固定为 `20260415,20260416,20260417,20260418,20260419`，每个 seed 跑一轮完整 `3 x 2` matrix。
- campaign runner 只控制 `seed / run-root / port-base`，其它后端资源参数原样透传给 `run_suite_b_matrix.py`，避免维护两套资源 CLI。
- campaign summary 新增 `aggregate.correctness_by_case`、`aggregate.ingest_p95_latency_delta_by_profile` 和 `aggregate.sqlite_unique_constraint_fail_count_by_case`。
- 正确性指标聚合 `mean / median / min / max`；入口 p95 护栏按 run-level delta 聚合，不把所有请求混成一个大样本重算 p95。

## Verification

- `python3 -m unittest run_suite_b_campaign_unit_test.py`
- `python3 -m unittest discover -s . -p '*_unit_test.py'`
- `python3 -m py_compile run_suite_b_matrix.py run_suite_b_campaign.py sender.py evaluator.py run_suite_b.py profiles.py sender_unit_test.py evaluator_unit_test.py run_suite_b_unit_test.py run_suite_b_matrix_unit_test.py run_suite_b_campaign_unit_test.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 正式 benchmark 不应该只跑一次。单次结果适合开发调参，但论文图表最好用固定 seed 复跑，避免被偶发调度抖动质疑。
- p95 这类尾延迟指标不要简单“所有请求一起平均”。更稳的做法是每轮先算 p95，再对 run-level p95 做 median/min/max。

### Function Explanation

- `run_suite_b_campaign.py`：多 seed 外层编排器，不直接理解后端资源参数，只把它们透传给 matrix runner。
- `build_matrix_argv()`：给每轮 matrix 注入独立 seed、端口基准和 run-root 前缀。
- `build_campaign_aggregate()`：把多轮 matrix summary 聚合成论文可用的 campaign summary。

### Pitfalls

- campaign 层不能允许用户再手动透传 `--seed / --run-root / --output-summary` 给 matrix，否则一轮复跑里会出现两个控制源，结果目录和 seed 口径会乱。
- `--port-stride` 要大于单轮 matrix 的 case 数。当前 `3 x 2` 是 6 个端口，默认 `20` 留了余量，避免相邻 seed 的 case 端口撞车。
