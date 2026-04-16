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

---

# 2026-04-16 fix(benchmark): 修正 Suite A drain 完成判定

## Git Commit Message

`fix(benchmark): 修正 Suite A drain 完成判定`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- `wait_until_sqlite_stable()` 新增 `expected_trace_count` 参数。现在只要 Suite A sender 已知固定 `trace_count`，drain 完成语义就不再是“SQLite 计数稳定几轮”，而是“`trace_summary` 追到目标 trace 数或超时”。
- `run_suite_a_case()` 现在会把 sender 的 `trace_count` 传给 drain 轮询逻辑，避免 clean 流量场景下把中间态误记成 `sqlite_counts_final`。
- `run_suite_a_case_unit_test.py` 补两条回归：
  - 锁 `run_suite_a_case()` 必须把 `expected_trace_count` 透传给等待逻辑；
  - 锁 `wait_until_sqlite_stable()` 在目标 trace 数未达成前，即使 SQLite 计数暂时稳定，也不能提前返回。
- benchmark README 和总览文档同步改口径，明确 Suite A 现在的 `drain_tail_ms` 是“补齐到目标 trace 数的尾巴”，不是“稳定轮询尾巴”。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py`
- `python3 server/tests/benchmark/suite_a/run_suite_a_case.py --server-command 'taskset -c 2-3 ./server/build-main/LogSentinel --db {sqlite_db} --port {port} --disable-ai --disable-webhook --server-io-threads 1 --worker-threads 32 --dispatch-worker-threads 1' --run-root /tmp/suite_a_main_ai_off_gap10_verify --port-base 18216 --trace-count 800 --spans-per-trace 8 --inter-trace-gap-ms 10 --send-workers 1 --request-timeout-ms 1000 --poll-interval-ms 50 --stable-rounds 3 --confirm-sleep-ms 100 --max-drain-wait-ms 30000`
- 实测结果从旧脚本会提前停在 `sqlite_counts_final=742` 一类中间态，修正后同场景已经能追到 `sqlite_counts_final=800`

## Learning Tips

### Newbie Tips

- “稳定”不等于“完成”。只要实验本身已经知道固定分母，最稳的收口条件永远是“追到目标值或超时”，不是看到计数暂时不动就收工。
- benchmark 脚本本身也会引入测量误差。看到结果反直觉时，先别急着怪后端，有时候是 runner 自己把中间态误标成 final。

### Function Explanation

- `wait_until_sqlite_stable(..., expected_trace_count=...)`：当调用方知道目标 trace 数时，函数会持续轮询到 `trace_summary` 达标；只有没传目标值时，才回退到旧的“稳定若干轮”模式。
- `run_suite_a_case()`：先记 `t_stop` 和 stop 时刻快照，再把 sender 的目标 trace 数带进 drain 等待，保证 `sqlite_counts_final` 真的是“补齐后的最终主数据状态”。

### Pitfalls

- 不能把 `sqlite_counts_final` 这个字段名字当成天然正确。字段名只有在收口条件对的时候才有意义；条件错了，字段名再像 final 也只是中间态。
- clean 流量场景里，如果 sender 分母固定，`stable_rounds / confirm_sleep_ms` 这类参数就不再是主收口逻辑，只是 legacy fallback。继续按它们解释主图，会把结论讲歪。
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

---

# 2026-04-16 feat(benchmark): 落 Suite A fixed sender 第一版

## Git Commit Message

`feat(benchmark): 落 Suite A fixed sender 第一版`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- 新增 `run_suite_a_case.py`，先把 Suite A 主图的 fixed clean sender + SQLite evaluator 收成一个单脚本，不再复用 `wrk` 当主发生器。
- sender 侧只保留 clean trace 语义：固定 `trace_count / spans_per_trace / inter_trace_gap_ms / send_workers`，每条 trace 最后一个 span 用 `trace_end=true` 收口。
- evaluator 侧只读查询 `trace_summary / trace_span`，并按 `poll_interval_ms / stable_rounds / confirm_sleep_ms / max_drain_wait_ms` 计算 `drain_tail_ms`。
- 当前结果 JSON 先固定输出：
  - `visible_completion_rate_at_stop`
  - `drain_tail_ms`
  - `sqlite_counts_at_stop`
  - `sqlite_counts_final`
  - `sender_stats`
- benchmark README 已补 `run_suite_a_case.py` 入口说明，避免后面继续把 Suite A 主图和旧 `run_suite_a.sh` 的 wrk wrapper 混在一起。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- 最小 dry-run：
  - 启临时本地 HTTP 202 server
  - 建只含 `trace_summary / trace_span` 的空 SQLite
  - 真跑 `run_suite_a_case.py` 一轮 4 trace / 3 spans 的小样本

## Learning Tips

### Newbie Tips

- 如果实验的主问题是“给定一批 trace，谁更快把主数据真正落完”，那 `wrk` 这种闭环压测器很容易把分母测脏。固定 sender 更适合这种题。
- `visible_completion_rate_at_stop` 和 `drain_tail_ms` 这种指标，不需要先把后端所有埋点都接进来。先用 sender 自己维护的分母，加 SQLite 主数据表做分子，就已经够回答主图问题。

### Function Explanation

- `send_clean_traces()`：按 trace 粒度调度 clean 流量，负责维护 `t_stop` 和发送端统计。
- `read_sqlite_counts()`：只读查询 `trace_summary / trace_span` 计数，不碰 analysis 等后置表。
- `wait_until_sqlite_stable()`：轮询 SQLite 主数据计数，直到计数稳定，再给出 `drain_tail_ms`。
- `run_suite_a_case()`：串起 sender、stop 时刻快照和 drain 等待，统一落 Suite A 结果 JSON。

### Pitfalls

- 不要把 `t_stop` 记成“SQLite 稳定时间”。`t_stop` 只认发送阶段结束；`drain_tail_ms` 才是专门量后链路尾巴的。
- 不要让 evaluator 长时间持有 SQLite 读事务。WAL 能缓解读写互挡，但长事务仍然会把 checkpoint 拖住，最后把实验自己测脏。

---

# 2026-04-16 feat(benchmark): 补 Suite A 单脚本自起后端与时间后缀产物

## Git Commit Message

`feat(benchmark): 补 Suite A 单脚本自起后端与时间后缀产物`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- `run_suite_a_case.py` 现在支持 auto-start 模式：传 `--server-bin` 或 `--server-command` 后，不再要求手动先起后端。
- `--run-root` 现在被解释成实验前缀；每次运行都会自动追加时间后缀，并从真实 run 目录自动派生：
  - `suite_a.db`
  - `result.json`
  - `server.log`
- 当前 baseline 的默认启动口径已经写进脚本：
  - AI 开
  - `--trace-ai-provider mock`
  - `--auto-start-proxy`
  - `--disable-webhook`
- 新增 `resolve_run_artifacts()` 和 `resolve_server_command()`，把路径派生和默认命令拼装从主流程里拆出来，方便单测锁口径。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- 最小自起后端 dry-run：
  - 通过 `--server-command "python3 fake_backend.py {port} {sqlite_db}"` 起临时 fake backend
  - 验证 auto-start、时间后缀目录、自动派生 `sqlite-db/result-json/server-log` 全部生效
- `git diff --check`

## Learning Tips

### Newbie Tips

- 如果实验入口既要负责“发流量”，又要负责“自起后端”，那路径管理一定要先收口，不然最容易发生的就是复用旧 DB/旧 JSON，把实验资产直接测脏。
- `run-root` 更适合表达“实验前缀”，不是“固定结果目录”。只要有 SQLite 这种状态文件，复跑同一条命令就应该默认新开一轮目录。

### Function Explanation

- `resolve_run_artifacts()`：把 `run-root` 变成带时间后缀的真实目录，并自动派生 DB/JSON/log 路径。
- `resolve_server_command()`：给 Suite A baseline 自动拼默认后端命令，不再要求外层手工凑参数。
- `launch_server_process()/wait_for_port_ready()/stop_server_process()`：负责最小起停与 ready 检查，不让 sender 在后端未 ready 时就开始打流量。

### Pitfalls

- auto-start 模式和手动模式是两套入口，不要混着传。传了 `--server-bin/--server-command` 却还手写一套错误的 `--sqlite-db`，很容易把“自动派生路径”和“手工旧路径”掺到一起。
- 当前脚本只把 backend 的 `cpuset/io/dispatch/worker` 收进来了，sender/ai-proxy 的更细绑核还没继续往下做；这一步先解决“能复跑”，不假装已经把全部核拓扑自动化了。

---

# 2026-04-16 fix(benchmark): 补 Suite A 结果里的实际启动命令

## Git Commit Message

`fix(benchmark): 补 Suite A 结果里的实际启动命令`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- `run_suite_a_case.py` 在 auto-start 模式下，除了继续真正执行后端命令，现在还会把解析占位符后的最终命令落到结果 JSON，字段名是 `resolved_server_command`。
- 这个字段只在 auto-start 模式输出，手动模式不会伪造。
- 这样后面再看 compare_target 结果时，不需要只靠肉眼翻 `server.log` 猜命令到底有没有把 `sqlite_db / port / CLI` 真正带进去。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py`
- `python3 -m unittest discover -s server/tests/benchmark/suite_a -p '*_unit_test.py'`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 脚本里最容易把结果测脏的，不只是流量形状，还有“这轮实验到底启动了什么命令”这种元信息。如果不落盘，复盘时就会退化成猜。
- 模板命令和实际命令不是一回事。带 `{sqlite_db}`、`{port}` 这种占位符的字符串，只能说明“你本来想这么跑”，不能证明“进程真是这么起的”。

### Function Explanation

- `resolve_server_command()`：负责把模板命令或 baseline 默认命令展开成最终 shell 命令。
- `run_suite_a_case()`：现在会把这条最终命令保存到 `runtime_args.resolved_server_command`，并继续带到结果 JSON。

### Pitfalls

- 不要把 `server_command` 模板本身当成实验证据。真正该看的，是占位符替换后的 `resolved_server_command`。
- 这个字段只是帮助复盘和排脏，不会反过来保证旧版二进制一定支持你传进去的所有 CLI；CLI 是否生效，最终还是要和 `server.log`、SQLite 真值一起交叉看。

---

# 2026-04-16 feat(benchmark): 补 Suite A AI-off gap 扫描脚本

## Git Commit Message

`feat(benchmark): 补 Suite A AI-off gap 扫描脚本`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_scan.py`
- `server/tests/benchmark/suite_a/run_suite_a_scan_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_main_scan.sh`
- `server/tests/benchmark/suite_a/run_suite_a_cmp_scan.sh`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- 新增 `run_suite_a_scan.py`，负责把 `gap x repeat` 的多轮 Suite A 单 case 批跑收成统一入口，并输出 `summary.json`。
- 新增 `run_suite_a_main_scan.sh` 和 `run_suite_a_cmp_scan.sh` 两个固定 wrapper，分别锁主线 `build-main` 和旧版 `build-cmp` 的 AI-off 口径，避免再手工复制命令把 `--auto-start-proxy` 一类参数拆坏。
- 当前这两个 wrapper 默认固定：
  - `gap=25/20/15ms`
  - `repeat=5`
  - `trace_count=800`
  - `spans_per_trace=8`
  - `send_workers=1`
- Suite A 当前主图口径默认改成 `AI-off`，因为 mock AI 已经在 `600ms` 量级，而主数据 SQLite flush 只有 `2~4ms`，继续 AI-on 会把“buffered vs direct write”的存储差异压得很扁。
- scan runner 还补了一层目录创建兜底，避免显式指定嵌套 `output-json` 时，单轮 case 在写 `result.json` 直接 `FileNotFoundError`。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_scan_unit_test.py`
- `python3 -m unittest discover -s server/tests/benchmark/suite_a -p '*_unit_test.py'`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py server/tests/benchmark/suite_a/run_suite_a_scan.py server/tests/benchmark/suite_a/run_suite_a_scan_unit_test.py`
- `bash -n server/tests/benchmark/suite_a/run_suite_a_main_scan.sh`
- `bash -n server/tests/benchmark/suite_a/run_suite_a_cmp_scan.sh`
- `git diff --check`
- 最小 smoke：
  - `run_suite_a_main_scan.sh --gaps-ms 25 --repeats 1 --trace-count 2 --spans-per-trace 2`
  - `run_suite_a_cmp_scan.sh --gaps-ms 25 --repeats 1 --trace-count 2 --spans-per-trace 2`

## Learning Tips

### Newbie Tips

- 如果你发现自己总是在同一条超长 benchmark 命令里手滑，把某个关键参数拆断，那说明这条命令已经不适合继续手敲了，应该尽快收成 wrapper。
- 对比实验里“参数本身是否正确传进去了”属于实验资产的一部分，不比结果 JSON 次要。参数一旦传脏，整轮结果都不该拿来解释。
- 当 AI 延迟远大于主数据存储延迟时，AI-on 图更适合回答“完整功能是否稳定”，不适合回答“存储路径谁更快”。

### Function Explanation

- `run_suite_a_scan.py`
  - 负责展开 `gap x repeat`
  - 负责为每一轮派生独立 `run-root / output-json / port`
  - 负责把单轮 `run_suite_a_case.py` 结果聚合成按 gap 分组的 summary
- `run_suite_a_main_scan.sh`
  - 固定主线 `build-main` 的 AI-off 参数组
- `run_suite_a_cmp_scan.sh`
  - 固定旧版 `build-cmp` 的 AI-off 参数组

### Pitfalls

- `run_suite_a_case.py` 会自动创建“加时间后缀后的真实 run-root”，但如果外层 scan runner 显式塞了嵌套 `output-json`，父目录还是要先手工建好。
- 旧版 `build-cmp` 没有 `--disable-ai`。要关 AI，只能不传 `--auto-start-proxy` 和 `--trace-ai-provider`，不能想当然套用主线 CLI。
- wrapper 支持你在命令尾部继续覆盖参数，但 summary 里会同时保留“默认值”和“覆盖值”的参数序列；最终实际生效值还是按命令行“后者覆盖前者”的规则解释。

---

# 2026-04-16 feat(benchmark): 落 Suite A Stage 1 粗搜入口

## Git Commit Message

`feat(benchmark): 落 Suite A Stage 1 粗搜入口`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_search_stage1.py`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `server/src/main.cpp`
- `server/persistence/BufferedTraceRepository.h`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- 新增 `run_suite_a_search_stage1.py`，把 Suite A 的 4 核粗搜固定成 `Phase A(lifecycle+sweep) -> Phase B(buffer) -> Phase C(AI-on smoke)` 三段编排。
- 搜索脚本默认只打一个 clean gap 点位，不做全排列；stdout 只保留“每组一行摘要 + 最终 top-k”，完整明细统一落 `summary.json` 和各 case 的 `result.json`。
- 后端新增 benchmark-only CLI：
  - `--trace-primary-flush-span-threshold`
  - `--trace-primary-flush-interval-ms`
- 这两个 CLI 只在启动时临时覆盖 `BufferedTraceRepository` 主数据桶的 flush 水位和 flush 间隔，不写回 SQLite，不进入正式 Settings。
- benchmark README 和总览文档同步补 Stage 1 口径，明确为什么当前先做两阶段剪枝，而不是直接把 lifecycle/sweep/buffer/gap/AI 一起做笛卡尔积。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py run_suite_a_scan_unit_test.py run_suite_a_search_stage1_unit_test.py`
- `python3 -m unittest server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_scan.py server/tests/benchmark/suite_a/run_suite_a_search_stage1.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py server/tests/benchmark/suite_a/run_suite_a_scan_unit_test.py server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `git diff --check`
- `cmake --build server/build --target LogSentinel -j2`
  - 这一步没有拿到完整成功结果，失败原因不是代码报错，而是机器磁盘写满：
    - `/usr/bin/ranlib: ... No space left on device`
    - `/tmp/...s: No space left on device`
  - 但本次改动对应的对象文件已经编过去：
    - `server/build/CMakeFiles/LogSentinel.dir/src/main.cpp.o`
    - `server/build/CMakeFiles/persistence_module.dir/persistence/BufferedTraceRepository.cpp.o`

## Learning Tips

### Newbie Tips

- 参数搜索不要一上来就做全排列。变量一多，最先炸掉的通常不是算法，而是你的时间预算和复盘能力。
- “buffer 参数可调”不等于“产品语义要跟着开放”。如果这个参数只是为了 benchmark 排查，就应该停留在 CLI override，不要急着塞进 Settings。

### Function Explanation

- `run_stage1_search()`：负责串起 Phase A/B/C，并把每个候选的 repeat 结果折成统一 summary。
- `replace_or_append_option()`：在保留现有 Suite A case CLI 的前提下，局部替换 `trace-count` 这类需要按 phase 改写的参数。
- `BufferedTraceRepository::Config`
  - `primary_span_reserve` 控制主数据桶按量 flush 的触发点；
  - `primary_flush_interval_ms` 控制主数据桶按时 flush 的最长等待。

### Pitfalls

- 如果 base case 参数里还手工塞 `--trace-lifecycle-profile`、`--trace-primary-flush-*` 或 `--disable-ai`，搜索脚本必须拒绝；否则你看到的就不是搜索结果，而是“谁最后覆盖了谁”。
- 这轮编译失败不能误判成代码错误。日志已经说明是磁盘空间耗尽，和这次 CLI 接线本身不是一类问题。

---

# 2026-04-16 fix(benchmark): 修正 Suite A Stage 1 搜索脚本参数透传

## Git Commit Message

`fix(benchmark): 修正 Suite A Stage 1 搜索脚本参数透传`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- `run_suite_a_case.py` 现在显式接收并透传以下 benchmark-only 后端参数：
  - `--disable-ai`
  - `--disable-webhook`
  - `--disable-buffered-trace-repo`
  - `--trace-lifecycle-profile`
  - `--trace-sweep-interval-ms`
  - `--trace-primary-flush-span-threshold`
  - `--trace-primary-flush-interval-ms`
- 之前 `run_suite_a_search_stage1.py` 会把这些参数塞给 `run_suite_a_case.py`，但后者不认识，于是还没起后端就在 Python 参数解析阶段先炸了。
- 现在这些参数在 auto-start 模式下会继续拼进最终后端启动命令；手动 sender/evaluator 逻辑本身不消费它们。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py run_suite_a_search_stage1_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py server/tests/benchmark/suite_a/run_suite_a_search_stage1.py server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`

## Learning Tips

### Newbie Tips

- 外层 benchmark 编排脚本和单 case runner 之间，最容易断的是“参数边界”。外层以为自己在切实验变量，内层其实根本没接，这种错会比纯代码 bug 更隐蔽。

### Function Explanation

- `build_server_passthrough_args()`：把只属于后端启动命令的 benchmark 参数统一收口，避免 `resolve_server_command()` 里到处散着拼接 if。

### Pitfalls

- 这类参数如果只在搜索脚本里认识、单 case runner 不认识，错误会发生在 Python 参数解析阶段，看起来像“命令行有问题”，其实是脚本边界没接上。

---

# 2026-04-16 fix(benchmark): 把 Suite A Stage 1 收成 protected-only 搜索

## Git Commit Message

`fix(benchmark): 把 Suite A Stage 1 收成 protected-only 搜索`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_search_stage1.py`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `server/src/main.cpp`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260416-fix-trace-timeout.md`

## Summary

- Suite A Stage 1 不再默认把 `minimal` 拉进候选池，而是固定 `protected`，先扫：
  - `sealed_grace_window_ms`
  - `sweep_tick_ms`
- 然后再在最佳 protected 底座上扫 buffer：
  - `trace_primary_flush_span_threshold`
  - `trace_primary_flush_interval_ms`
- 后端新增 benchmark-only CLI：`--trace-sealed-grace-window-ms`，只用于实验，不写回 SQLite Settings。
- `run_suite_a_case.py` 现在会把 `trace-sealed-grace-window-ms` 和现有 lifecycle/sweep/flush 开关一起透传给 auto-start 的后端命令。
- README 和 benchmark 总览同步改口径，明确 Stage 1 的目标已经从“谁数值最好”收紧成“能不能把 protected+buffered 这套架构调得更像样”。

## Verification

- `cd server/tests/benchmark/suite_a && python3 -m unittest run_suite_a_case_unit_test.py run_suite_a_search_stage1_unit_test.py`
- `python3 -m unittest server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py server/tests/benchmark/suite_a/run_suite_a_search_stage1.py server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `cmake --build server/build --target LogSentinel -j1`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 搜索脚本的目标要和论文叙事一致。你如果想证明 protected 架构能调优，就不能先让 minimal 把冠军抢走，再拿这个冠军回头讲 protected。

### Function Explanation

- `trace-sealed-grace-window-ms`
  - 这是 protected 生命周期里 sealed grace 的 CLI override；
  - 它控制的是 trace 明确封口后，还愿意额外等多久来吸收晚到 span。
- `run_stage1_search()`
  - 现在的 Phase A 先把“生命周期固定成本”单独扫出来；
  - Phase B 才去看 buffer 是不是还能继续抠掉尾巴。

### Pitfalls

- 如果只调 buffer，不调 sealed grace，那么 protected 的固定等待会把结果平台化，最后你会误以为“buffer 怎么调都没用”。
