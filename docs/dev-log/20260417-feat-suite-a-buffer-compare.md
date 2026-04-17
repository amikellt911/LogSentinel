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

---

# 2026-04-17 chore(benchmark): 冻结 Suite A 4核与16核 wrapper 命令

## Git Commit Message

`chore(benchmark): 冻结 Suite A 4核与16核 wrapper 命令`

## Modification

- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_4c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_16c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_4c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_16c.sh`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1_4c.sh`
- `server/tests/benchmark/suite_a/suite_a_frozen_wrappers_unit_test.py`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 5 个 frozen wrapper，把 Suite A 当前真正要反复复跑的命令收成脚本，不再靠手抄长命令。
- `run_suite_a_main_vs_cmp_4c.sh / run_suite_a_main_vs_cmp_16c.sh` 固定主叙事口径：`cmp_baseline vs main_tuned protected buffered`。
- `run_suite_a_buffer_compare_4c.sh / run_suite_a_buffer_compare_16c.sh` 固定辅助归因口径：`buffered vs no-buffer`，并只保留 `512/5` 这组已知最稳的 flush 点位。
- `run_suite_a_search_stage1_4c.sh` 固定 4 核调参入口，后面只有在明确要重搜 tuned 参数时才重新启用。
- benchmark README 和总览文档同步改口径，把 16 核正式节奏也一起冻结，不再留“之后再推”的悬而未决状态。
- 新增 wrapper 单测，锁文件名和关键默认参数，防止以后有人改脚本时把正式口径悄悄改漂。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_a/suite_a_frozen_wrappers_unit_test.py`
- `bash -n server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_4c.sh server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_16c.sh server/tests/benchmark/suite_a/run_suite_a_buffer_compare_4c.sh server/tests/benchmark/suite_a/run_suite_a_buffer_compare_16c.sh server/tests/benchmark/suite_a/run_suite_a_search_stage1_4c.sh`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 最怕的不是脚本写错，而是“命令散落在聊天记录里”。只要结论要进论文，就必须把命令冻结成 wrapper，不然一周后你自己都不敢保证复跑口径没漂。
- 资源拓扑和负载点应该跟脚本一起冻结。只写文档、不写 wrapper，最后还是会回到手抄参数的老路。

### Function Explanation

- `run_suite_a_main_vs_cmp_4c.sh / run_suite_a_main_vs_cmp_16c.sh`
  - 固定主叙事的版本对比入口和三档负载点。
- `run_suite_a_buffer_compare_4c.sh / run_suite_a_buffer_compare_16c.sh`
  - 固定辅助归因入口和当前保留的唯一 flush 点位。
- `suite_a_frozen_wrappers_unit_test.py`
  - 读取 wrapper 文本，确认 runner 名称和关键默认值没有被改掉。

### Pitfalls

- 16 核 wrapper 里给后端留了更宽的 cpuset，但 sender 本身没有单独 `taskset`；如果你后面真要做“严格 sender 两核独占”，要另起一层外部调度脚本，而不是偷偷改当前 frozen wrapper。
- 调参 wrapper 和正式结果 wrapper 不是一回事。不要拿 `run_suite_a_search_stage1_4c.sh` 跑出一个新点位后，没复核就直接替换正式结果图。

---

# 2026-04-17 refactor(benchmark): 抽共享 SQLite 轮询 helper

## Git Commit Message

`refactor(benchmark): 抽共享 SQLite 轮询 helper`

## Modification

- `server/tests/benchmark/common/utils/trace_sqlite_polling.py`
- `server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`

## Summary

- 新增共享 helper `trace_sqlite_polling.py`，把 SQLite 只读计数和“先追平 expected trace count，再判断是否稳定”的 drain 语义从 Suite A 私有实现里抽出来。
- 新增 `trace_sqlite_polling_unit_test.py`，锁两条最容易回归的行为：达到目标 trace 数前不能提前返回，timeout 时必须回最后一份可见计数。
- `run_suite_a_case.py` 改成复用共享 helper，不再自己维护重复的 SQLite 轮询逻辑。
- 为了兼容 `python3 server/tests/benchmark/suite_a/run_suite_a_case.py` 这种脚本直跑方式，在 Suite A 入口里补了共享 helper 的导入路径处理。

## Verification

- `python3 -m unittest server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/common/utils/trace_sqlite_polling.py server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `git diff --check -- server/tests/benchmark/common/utils/trace_sqlite_polling.py server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py docs/todo-list/Todo_Benchmark.md`

## Learning Tips

### Newbie Tips

- 轮询 SQLite 排空时，真正危险的不是“多等了一会儿”，而是“过早返回”。一旦脚本把中间态当成最终态，后面所有 completion ratio 和 drain tail 都会被污染。
- 共享 helper 的价值不是少写几十行代码，而是把 drain 语义固定住。Suite A 和后面的 Suite D 都用同一套 helper，后面对比结果才不会因为脚本口径漂移而失真。

### Function Explanation

- `read_sqlite_counts()`：只做两张表的只读计数查询，返回 `trace_summary / trace_span`。
- `wait_until_sqlite_stable()`：如果调用方给了 `expected_trace_count`，就先追平目标值；否则才走“连续稳定若干轮”的通用稳定判定。
- `COMMON_UTILS_DIR` 导入逻辑：解决脚本直接执行时，Python 只认识当前目录、不认识兄弟目录的问题。

### Pitfalls

- 如果 helper 在 timeout 分支不回最后一次可见 counts，benchmark 结果只会剩一个布尔值 `timeout=true`，根本不知道系统卡在什么位置。
- 如果脚本直跑时不处理共享 helper 的导入路径，单测可能是绿的，但真实命令 `python3 server/tests/benchmark/suite_a/run_suite_a_case.py ...` 会直接在 import 阶段炸掉。
