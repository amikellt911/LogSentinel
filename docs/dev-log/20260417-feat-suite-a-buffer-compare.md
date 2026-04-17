# 2026-04-17 feat(benchmark): 增加 Suite A buffer 对比脚本

## Git Commit Message

`feat(benchmark): 增加 Suite A buffer 对比脚本`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare.py`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 `run_suite_a_buffer_compare.py`，固定 `protected + grace=100 + sweep=200` 基线，直接比较 `disable_buffered_trace_repo` 和一组 tuned buffered flush 点位。
- compare runner 默认只输出每个候选的一行摘要和最终 top-k，避免 benchmark 输出太长；详细结果继续写入 `summary.json`。
- compare runner 会自己接管 `disable-buffered / lifecycle / grace / sweep / flush / run-root / output-json / port-base` 这些对比变量，避免调用方在 case args 里把口径覆盖脏掉。
- 新增 `run_suite_a_buffer_compare_unit_test.py`，锁默认候选矩阵、case 顺序、排序口径和精简输出格式。
- `Todo_Benchmark` 追加并完成本轮 compare runner 任务记录。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_a/run_suite_a_buffer_compare_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_buffer_compare.py server/tests/benchmark/suite_a/run_suite_a_buffer_compare_unit_test.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 做对比脚本时，最容易犯的错不是代码写挂，而是把两个实验变量混在一起。这里必须把生命周期、发送规模、gap、端口分配全部钉死，只让 repository 策略变化。
- benchmark 终端输出不等于结果资产。终端只保留“人眼要快速判断”的摘要，完整 JSON 继续落盘，这样复跑和论文整理都更稳。

### Function Explanation

- `build_candidates()`：生成固定候选矩阵，先放 `disable_buffered`，再放 buffered flush 组合。
- `build_case_argv()`：把 compare 级变量注入到单次 `run_suite_a_case.py` 调用里，并为每个候选/重复轮次派生独立目录和端口。
- `candidate_sort_key()`：先比较 `visible_completion_rate_at_stop`，再比较 `drain_tail_ms`，最后才用保守 tie-break 处理完全接近的候选。

### Pitfalls

- 不能把 `--disable-buffered-trace-repo` 留给调用方自己传，否则 compare runner 就失去“同口径编排”的意义了。
- 如果把 flush 参数也传给 `disable_buffered` case，会让结果解释变脏，因为这组参数在直写 SQLite 路径下根本不生效。
- top-k 排序如果先看 drain，再看 visible，会把“尾巴略短但停表时少看到了更多数据”的候选排到前面，结论会偏。

---

# 2026-04-17 feat(benchmark): 增加 Suite A 新旧版本叙事对比脚本

## Git Commit Message

`feat(benchmark): 增加 Suite A 新旧版本叙事对比脚本`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py`
- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 `run_suite_a_main_vs_cmp.py`，默认固定三档负载点：`light(800/20/1)`、`mid(1600/10/2)`、`heavy(3200/5/2)`。
- compare runner 只保留两条版本线：`cmp_baseline` 和 `main_tuned`。前者代表旧版历史口径，后者代表当前 `protected + grace=100 + sweep=200 + flush=512/5` 的主线调优口径。
- runner 内部自己拼装 `build-cmp` 和 `build-main` 的启动命令，避免手工抄命令时把 `disable-ai / disable-webhook / lifecycle / flush` 参数拆坏。
- 终端输出压缩成“每档负载一行 + overall 一行”，详细 run 结果继续落到 `summary.json`。
- 单测锁住三档负载默认值、双版本命令拼装和 `delta_main_vs_cmp = main - cmp` 的汇总语义。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_unit_test.py`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 论文主叙事和内部归因实验最好拆开。`main vs cmp` 回答的是“新能力带来的总体成本”，`buffered vs no-buffer` 回答的是“当前主线内部到底是谁在背锅”，这两道题不要混成一张图。
- 负载点比小参数更重要。既然已经找到一组能站住脚的 tuned 参数，后面更该扩 `light/mid/heavy`，而不是继续卷 `64/5`、`128/5` 这种小点位。

### Function Explanation

- `parse_load_points()`：把 `label:trace_count:gap_ms:send_workers` 解析成 story compare 的负载矩阵。
- `build_server_command()`：分别为 `cmp_baseline` 和 `main_tuned` 生成稳定的后端启动命令。
- `build_load_summary()`：把同一档负载下两个版本的聚合结果折叠成一个摘要，并计算 `main - cmp` 形式的 delta。

### Pitfalls

- 不能把 `--server-command`、`--trace-count`、`--send-workers` 这类变量再开放给外层 case args；一旦被覆盖，按 load 聚合的真值就会立刻脏掉。
- delta 语义必须固定。如果一会儿用 `cmp-main`，一会儿用 `main-cmp`，最后表格里负号的含义会彻底乱掉。
