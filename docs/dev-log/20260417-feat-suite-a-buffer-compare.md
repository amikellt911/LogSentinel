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

# 2026-04-17 fix(benchmark): 修复 Suite D scaling 的 cpuset 基址假设

## Git Commit Message

`fix(benchmark): 修复 Suite D scaling 的 cpuset 基址假设`

## Modification

- `server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py`
- `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- `run_suite_d_scaling.py` 新增 `--core-base-offset`，让主扩展曲线 runner 不再默认把 sender/backend 的 cpuset 从 `0` 号核开始生成。
- `build_case_args()` 现在会用 `core_base_offset + sender_cores/backend_cores` 推导 `wrk_cpuset / server_cpuset`，从而支持容器常见的 `160-191` 这类高位 CPU 区间。
- `summary.json` 的元数据里追加 `core_base_offset`，避免后面回看资产时只看到 `total_cores=24`，却不知道这轮实际绑的是哪一段核位。
- `run_suite_d_scaling_24c.sh` 暴露 `SUITE_D_CORE_BASE_OFFSET` 环境变量，并把它透传给 scaling runner；云机上只需要设成 `160`，不必再手工改 Python 脚本。
- 单测同步锁住新行为：一方面验证 offset=160 时 `4` 核点位会派生出 `wrk=160 / server=161-163`，另一方面验证 frozen wrapper 确实保留了这个入口。

## Verification

- `python3 -m unittest server.tests.benchmark.suite_d.run_suite_d_scaling_unit_test`
- `python3 -m unittest server.tests.benchmark.suite_d.suite_d_frozen_wrappers_unit_test`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_scaling.py server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `bash -n server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 脚本里最隐蔽的一类坑不是参数值错，而是“默认假设 CPU 一定从 0 开始”。在裸机上这常常没事，但进容器后，cpuset 经常会被映射到高位核区间。
- 如果 runner 会自动派生 `taskset`，那 `cpuset` 的起点就属于实验口径的一部分，必须落进结果元数据；否则以后回看 `summary.json`，你只能看到“24 核”，看不到“到底用了哪 24 个核”。

### Function Explanation

- `--core-base-offset`：Suite D scaling runner 新增的 CLI，用来指定 sender/backend 切核时的起始核位。
- `build_cpuset()`：继续负责把“起点 + 核数”展开成 `160-163` 这种 shell 可直接吃的 cpuset 文本。
- `SUITE_D_CORE_BASE_OFFSET`：24 核 frozen wrapper 暴露的环境变量入口，云机只改这一个值就能适配不同容器核位映射。

### Pitfalls

- 只改 wrapper、不改 `run_suite_d_scaling.py` 没用，因为真正自动生成 cpuset 的逻辑在 Python runner 里。
- 只改 Python runner、不补 frozen wrapper 入口也不够，因为正式复跑命令还是会回到手工传一串参数，最后口径照样容易漂。

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

---

# 2026-04-17 feat(benchmark): 增加 Suite D 单 case runner

## Git Commit Message

`feat(benchmark): 增加 Suite D 单 case runner`

## Modification

- `server/tests/benchmark/suite_d/trace_model_suite_d.lua`
- `server/tests/benchmark/suite_d/run_suite_d_case.py`
- `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 `trace_model_suite_d.lua`，只保留 Suite D 需要的 clean end-trace 发送语义，并在 `done()` 阶段额外打印 `offered_traces / spans_per_trace / latency_p95_ms / latency_p99_ms`。
- 新增 `run_suite_d_case.py`，负责自动派生 `run_root / sqlite_db / server_log / result.json`，启动后端，先跑 warmup，再跑正式 wrk measurement。
- 单 case runner 在 measurement 结束后记录 `t_stop`，读取 SQLite stop 快照，再调用共享 polling helper 等待 `trace_summary` 追平 `offered_traces`，从而同时给出窗口内完成量和 drain 尾巴。
- runner 输出统一结果 JSON，收口 `wrk_metrics / sqlite_counts_at_stop / sqlite_counts_final / online_completed_traces_per_sec / online_completion_ratio / drain_tail_ms / drain_timeout`。
- 新增 `run_suite_d_case_unit_test.py`，锁 CLI 参数、wrk 摘要解析和单 case 编排结果，避免后面拓扑搜索和主曲线 runner 接进来时把底层口径改漂。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_case.py server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py server/tests/benchmark/common/utils/trace_sqlite_polling.py`
- `python3 server/tests/benchmark/suite_d/run_suite_d_case.py --help`
- `git diff --check -- server/tests/benchmark/suite_d/trace_model_suite_d.lua server/tests/benchmark/suite_d/run_suite_d_case.py server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`

## Learning Tips

### Newbie Tips

- wrk 的 `QPS` 只是请求级速率，不是 trace 级完成量。Suite D 要看的是 measurement window 结束时 SQLite 已经真正补齐了多少条 trace，所以必须自己补 `offered_traces` 这个分母。
- `t_stop` 必须认“measurement window 停发”的时刻，而不是“SQLite 最终追平”的时刻。否则你会把停发后的排空时间偷偷塞回主窗口，图上看起来更好看，但结论是假的。

### Function Explanation

- `parse_wrk_metrics()`：从 wrk stdout 里同时摘 `requests / requests_per_sec` 和 Suite D Lua 额外打印的 `offered_traces / latency_p95_ms / latency_p99_ms`。
- `build_wrk_command()`：固定 Suite D 的 wrk 调用骨架，继续走 clean end-trace，并支持 `taskset` 给 sender 绑核。
- `run_suite_d_case()`：把“起服务 -> warmup -> measurement -> 记 t_stop -> SQLite stop 快照 -> drain 追平 -> 写 result.json”这一整条时间线收进一个最小入口。

### Pitfalls

- 如果直接复用 `common/wrk/trace_model.lua` 而不补 Suite D summary 行，后面主曲线就只能看到请求级 QPS，看不到 trace 级 offered 分母，结果没法讲。
- 如果单 case runner 在 warmup 之后不清楚地区分“measurement stop counts”和“final drain counts”，拓扑搜索和主曲线会把窗口内能力和尾巴长度混成一个指标，排序结果会很脏。

---

# 2026-04-17 feat(benchmark): 增加 Suite D 拓扑搜索 runner

## Git Commit Message

`feat(benchmark): 增加 Suite D 拓扑搜索 runner`

## Modification

- `server/tests/benchmark/suite_d/run_suite_d_topology_search.py`
- `server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 `run_suite_d_topology_search.py`，把 `24` 核拓扑搜索收口成固定的 `T1/T2/T3` 三候选，不再让 Suite D 重新膨胀成大矩阵调参。
- runner 固定 `sender=4 / backend=20`、`wrk_cpuset=0-3`、`server_cpuset=4-23` 和 `AI-off`，只比较 `server_io_threads / dispatch_worker_threads / worker_threads` 这三个拓扑差异。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_topology_search.py server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`
- `python3 server/tests/benchmark/suite_d/run_suite_d_topology_search.py --help`
- `git diff --check -- server/tests/benchmark/suite_d/run_suite_d_topology_search.py server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`

## Learning Tips

### Newbie Tips

- 拓扑搜索不是把所有线程参数全排列暴力枚举。主图真正要的是“统一部署策略下，资源加上去以后会不会继续涨”，所以这里只做很小的代表性候选搜索。
- `AI-off` 时 `worker_threads` 不再背 AI 调用这类长尾阻塞，真正更值得优先怀疑的是 `server_io_threads` 和 `dispatch_worker_threads`。

### Function Explanation

- `build_candidates()`：固定产出 `T1/T2/T3` 三个候选，不把搜索面继续放大。
- `candidate_sort_key()`：先按 `online_completed_traces_per_sec`，再按 `online_completion_ratio`，最后才比较 `drain_tail_ms`。
- `run_topology_search()`：只负责编排候选、调用单 case runner、汇总摘要，不自己处理 wrk 或 SQLite 细节。

### Pitfalls

- 如果每个总核数点位都独立重搜最优拓扑，后面老师一问“到底是核数变了还是参数变了”，整张主曲线就讲不干净了。
- 如果拓扑搜索输出一上来就打印整块 JSON，批跑时终端几乎没法看，人只能重新翻结果文件。

---

# 2026-04-17 feat(benchmark): 冻结 Suite D 主曲线入口

## Git Commit Message

`feat(benchmark): 冻结 Suite D 主曲线入口`

## Modification

- `server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py`
- `server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh`
- `server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh`
- `server/tests/benchmark/suite_d/run_suite_d.sh`
- `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 新增 `run_suite_d_scaling.py`，把 Suite D 主扩展曲线固定成 `4/8/12/16/20/24` 六个总核数点位，并冻结 sender/backend 核数拆分、T2 拓扑比例映射和水位派生规则。
- scaling runner 统一按 `sender` 从 0 号核开始、`backend` 紧跟其后 的方式自动派生 `wrk_cpuset / server_cpuset`，避免每个点位再手抄绑核命令。
- scaling runner 统一派生 `worker_queue_size / trace_active_session_limit / trace_buffered_span_limit`，这样主曲线比较的是“总预算变大后的统一部署策略”，不是每个点各自临时拍参数。
- 新增 `run_suite_d_local4_ai_on.sh`、`run_suite_d_topology_search_24c.sh`、`run_suite_d_scaling_24c.sh` 三个 frozen wrapper，分别固定本机 4 核完整链路证明图、24 核拓扑搜索和 24 核主扩展曲线入口。
- `run_suite_d.sh` 明确改口为 generic wrapper，只给历史 common runner 兜底，避免再被误当成论文正式命令。
- 新增两组单测：`run_suite_d_scaling_unit_test.py` 锁主曲线的总核数/拓扑/水位派生和摘要格式；`suite_d_frozen_wrappers_unit_test.py` 锁 3 个 frozen wrapper 与 generic wrapper 的关键命令片段。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- `python3 server/tests/benchmark/suite_d/run_suite_d_scaling.py --help`
- `bash -n server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh server/tests/benchmark/suite_d/run_suite_d.sh`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 主扩展曲线不要把“每个点单独调到最优”当成优点。那样画出来的不是扩展曲线，而是六套不同系统拼成的一张图。
- 这里的 `worker_threads` 不是 CPU 并行度承诺，它只是阻塞槽位；真正决定 sender 和 backend 怎么分家的，是总核数预算和绑核范围。

### Function Explanation

- `DEFAULT_CORE_SPLIT`：冻结主曲线的总核数拆分，回答“这档预算下 sender/backend 各拿多少核”。
- `DEFAULT_TOPOLOGY_MAP`：冻结 T2 拓扑的比例映射，回答“这档预算下 io/dispatch/worker 线程怎么配”。
- `build_case_args()`：把总核数点位自动派生为单 case runner 所需的完整命令参数，包括绑核、水位、结果目录和端口。
- `run_scaling()`：顺序跑六个点位，输出简明摘要，并把完整结果聚合进 `summary.json`。

### Pitfalls

- 如果不把 generic wrapper 明确标成“非正式入口”，后面最容易发生的事就是有人图省事，直接拿旧 wrapper 去跑论文图，结果口径和新 runner 不一致。
- 如果主曲线里不冻结 sender/core split，而是每次靠人手改 cpuset，复跑时最容易把 4 核本机图和 24 核云机的 4 核档位混成一回事。

---

# 2026-04-17 feat(benchmark): 补 Suite D flamegraph AI-off 入口

## Git Commit Message

`feat(benchmark): 补 Suite D flamegraph AI-off 入口`

## Modification

- `server/tests/benchmark/common/run_flamegraph_case.sh`
- `server/tests/benchmark/suite_d/run_flamegraph.sh`
- `server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`
- `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`

## Summary

- 在 `run_flamegraph_case.sh` 新增 `SERVER_IO_THREADS / DISABLE_AI / DISABLE_WEBHOOK / NO_AUTO_START_PROXY` 四个环境开关，让 common flamegraph runner 可以复用到 Suite D 的 `AI-off` 解释图。
- common runner 的默认值保持老口径不变：如果不显式翻这些布尔位，仍然自动起 proxy、自动起 webhook mock，并继续走历史的 flamegraph 流程。
- `run_suite_d_flamegraph_24c.sh` 固定成 `24` 核、`AI-off`、Suite D 专用 Lua、`4` sender 核 + `20` backend 核、`io=6 / dispatch=4 / worker=32` 的 frozen wrapper，不再让 flamegraph 变成另一套参数搜索入口。
- `run_flamegraph.sh` 明确改成 generic wrapper，只负责把结果收进 `suite_d` 目录；论文正式解释图应走 `run_suite_d_flamegraph_24c.sh`。
- wrapper 单测追加 flamegraph 断言，锁定 `trace_model_suite_d.lua`、`SERVER_IO_THREADS` 和 `DISABLE_AI / DISABLE_WEBHOOK / NO_AUTO_START_PROXY` 这些关键片段，防止后面有人把口径改漂。

## Verification

- `python3 -m unittest server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `bash -n server/tests/benchmark/common/run_flamegraph_case.sh server/tests/benchmark/suite_d/run_flamegraph.sh server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`

## Learning Tips

### Newbie Tips

- flamegraph 不是新的 benchmark 主图，它只是解释主图热点的辅助证据。所以它最重要的要求不是“花样多”，而是和主曲线口径一致。
- 如果你想看的是 `AI-off` 后端主链热点，却还让脚本自动起 proxy、自动打 webhook，那火焰图里就会混进不属于这张图的问题。

### Function Explanation

- `SERVER_IO_THREADS`：把 flamegraph 的后端 I/O 线程数显式钉死，避免继续吃老脚本里的隐式默认值。
- `DISABLE_AI / DISABLE_WEBHOOK / NO_AUTO_START_PROXY`：用环境开关最小改 common runner，而不是再复制一份 Suite D 专用 flamegraph 主脚本。
- `run_suite_d_flamegraph_24c.sh`：把 Suite D 解释图需要的绑核、线程数、水位和 Lua 脚本一次性固定住。

### Pitfalls

- 如果 flamegraph wrapper 直接把 profile、拓扑和 AI 开关全暴露给调用方，这个 wrapper 很快就会退化成“又一个搜索入口”，不再是 frozen 命令。
- 如果 common runner 默认语义被这次改坏，Suite A 之前的 flamegraph 口径也会一起漂；所以这次必须保持默认值完全兼容，再让 Suite D wrapper 显式翻开关。

---

# 2026-04-17 docs(benchmark): 收口 Suite D 正式命令与口径

## Git Commit Message

`docs(benchmark): 收口 Suite D 正式命令与口径`

## Modification

- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

## Summary

- `README` 已把 Suite D 的正式入口明确成 4 个 frozen wrapper：本机 `4` 核 `AI-on` 证明图、`24` 核拓扑搜索、`24` 核主扩展曲线、`24` 核 `AI-off` flamegraph。
- `README` 也明确把 `run_suite_d.sh` 标成 generic wrapper，不再让它和正式论文命令混在一起。
- `BENCHMARK_SUITE_OVERVIEW` 的 Suite D 段落已经从“16 核锚点、还没定死”的旧设计，改成当前真正落地的冻结口径：
  - 两层叙事：本机 `4` 核 `AI-on` 证明图 + `24` 核 `AI-off` 主扩展曲线
  - 主指标：`online_completed_traces_per_sec`
  - `wrk` 继续做主发生器，但额外补 `offered_traces`
  - `24` 核只做 `T1/T2/T3` 小拓扑搜索，再按 `T2` 比例映射缩到 `4/8/12/16/20/24`
  - flamegraph 只是主曲线解释图，不再是另一套搜索入口
- 已跑最小 dry-run：
  - 命令：`python3 server/tests/benchmark/suite_d/run_suite_d_case.py ... --warmup-duration 1s --duration 2s --disable-ai --disable-webhook --no-auto-start-proxy`
  - artifact：`/tmp/suite_d_case_smoke-20260417-153553-740ms/result.json`
  - 关键字段：`offered_traces = 2083`、`online_completed_traces_per_sec = 480.0`、`drain_tail_ms = 0`

## Verification

- `python3 -m unittest server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `python3 -m py_compile server/tests/benchmark/common/utils/trace_sqlite_polling.py server/tests/benchmark/suite_d/run_suite_d_case.py server/tests/benchmark/suite_d/run_suite_d_topology_search.py server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- `bash -n server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`
- `python3 server/tests/benchmark/suite_d/run_suite_d_case.py --run-root /tmp/suite_d_case_smoke --server-bin ./server/build/LogSentinel --server-cpuset 1-2 --wrk-cpuset 0 --server-io-threads 1 --dispatch-worker-threads 1 --worker-threads 8 --worker-queue-size 4096 --trace-active-session-limit 512 --trace-buffered-span-limit 4096 --trace-max-dispatch-per-tick 64 --connections 20 --wrk-threads 1 --warmup-duration 1s --duration 2s --disable-ai --disable-webhook --no-auto-start-proxy`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 文档收口不是“把已经做过的事再抄一遍”，而是把已经冻结的命令、指标和解释边界写成别人不会再误解的样子。
- 最小 dry-run 的价值不是看数字漂不漂亮，而是看命令、结果目录和结果 JSON 三者是不是已经连成一条闭环。

### Function Explanation

- `run_suite_d_case.py`
  - Suite D 的统一单 case 编排器，后面的拓扑搜索和主曲线都基于它，不再各写一套 measurement/drain 逻辑。
- `run_suite_d_topology_search_24c.sh`
  - 固定 `24` 核 `AI-off` 小拓扑搜索入口，只负责 `T1/T2/T3`。
- `run_suite_d_scaling_24c.sh`
  - 固定 `24` 核主扩展曲线入口，统一按总核数预算驱动 sender/backend 拆分和拓扑映射。

### Pitfalls

- 如果总览文档还保留“16 核锚点、还没定死”的旧说法，后面别人按文档复跑时会直接和 frozen wrapper 打架。
- 如果最小 dry-run 不留 artifact 路径和关键字段，过几天再回头核对时，很容易又重跑一遍本来已经验证过的命令。

---

# 2026-04-17 feat(benchmark): 为结果文件补实验上下文资产

## Git Commit Message

`feat(benchmark): 为结果文件补实验上下文资产`

## Modification

- `server/tests/benchmark/common/utils/benchmark_metadata.py`
- `server/tests/benchmark/common/utils/benchmark_metadata_unit_test.py`
- `server/tests/benchmark/common/utils/write_flamegraph_summary.py`
- `server/tests/benchmark/common/utils/write_flamegraph_summary_unit_test.py`
- `server/tests/benchmark/common/run_flamegraph_case.sh`
- `server/tests/benchmark/suite_a/run_suite_a_case.py`
- `server/tests/benchmark/suite_a/run_suite_a_scan.py`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1.py`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare.py`
- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py`
- `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_scan_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_unit_test.py`
- `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_unit_test.py`
- `server/tests/benchmark/suite_b/run_suite_b.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- `server/tests/benchmark/suite_b/run_suite_b_campaign.py`
- `server/tests/benchmark/suite_b/run_suite_b_unit_test.py`
- `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`
- `server/tests/benchmark/suite_b/run_suite_b_campaign_unit_test.py`
- `server/tests/benchmark/suite_d/run_suite_d_case.py`
- `server/tests/benchmark/suite_d/run_suite_d_topology_search.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`
- `server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py`
- `server/tests/benchmark/README.md`
- `docs/BENCHMARK_SUITE_OVERVIEW.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

## Summary

- 新增共享 helper `benchmark_metadata.py`，统一负责三件事：
  - 运行时采集 `hostname / uname -a / lscpu`
  - 解析 `cpuset`
  - 把 `experiment_context / artifacts` 直接注入 benchmark 主 JSON
- `Suite A / Suite B / Suite D` 的代表性单 case runner 和所有正式 summary runner 现在都会在各自的 `result.json / summary.json` 里携带：
  - 机器信息
  - CPU 分配
  - 线程拓扑
  - workload
  - 有效实验参数
  - 产物路径
- `lscpu` 不可用时，会自动回退到 `nproc --all` 和 `/proc/cpuinfo`，至少把总逻辑核数和 CPU 型号补齐。
- `Suite B` 这次顺手补了脚本直跑/根目录单测共用的导入路径，避免 benchmark runner 只有“手工跑脚本”能 work，`python3 -m unittest` 却 import 失败。
- flamegraph 入口新增 `write_flamegraph_summary.py`，在保留原来 `run-summary.log` 的同时，再额外生成一份结构化的 `run-summary.json`。
- 文档已经同步改成新口径：
  - benchmark 主 JSON 不再只是业务指标
  - 结果资产默认自带 `experiment_context / artifacts`
  - flamegraph 现在也属于同一套实验资产体系

## Verification

- `python3 -m unittest server.tests.benchmark.common.utils.benchmark_metadata_unit_test server.tests.benchmark.common.utils.write_flamegraph_summary_unit_test server.tests.benchmark.suite_a.run_suite_a_case_unit_test server.tests.benchmark.suite_a.run_suite_a_scan_unit_test server.tests.benchmark.suite_a.run_suite_a_search_stage1_unit_test server.tests.benchmark.suite_a.run_suite_a_buffer_compare_unit_test server.tests.benchmark.suite_a.run_suite_a_main_vs_cmp_unit_test server.tests.benchmark.suite_b.run_suite_b_unit_test server.tests.benchmark.suite_b.run_suite_b_matrix_unit_test server.tests.benchmark.suite_b.run_suite_b_campaign_unit_test server.tests.benchmark.suite_d.run_suite_d_case_unit_test server.tests.benchmark.suite_d.run_suite_d_topology_search_unit_test server.tests.benchmark.suite_d.run_suite_d_scaling_unit_test`
- `python3 -m py_compile server/tests/benchmark/common/utils/benchmark_metadata.py server/tests/benchmark/common/utils/write_flamegraph_summary.py server/tests/benchmark/suite_a/run_suite_a_case.py server/tests/benchmark/suite_a/run_suite_a_scan.py server/tests/benchmark/suite_a/run_suite_a_search_stage1.py server/tests/benchmark/suite_a/run_suite_a_buffer_compare.py server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py server/tests/benchmark/suite_b/run_suite_b.py server/tests/benchmark/suite_b/run_suite_b_matrix.py server/tests/benchmark/suite_b/run_suite_b_campaign.py server/tests/benchmark/suite_d/run_suite_d_case.py server/tests/benchmark/suite_d/run_suite_d_topology_search.py server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- `bash -n server/tests/benchmark/common/run_flamegraph_case.sh server/tests/benchmark/suite_d/run_flamegraph.sh server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`
- `git diff --check`

## Learning Tips

### Newbie Tips

- benchmark 结果文件如果只存指标，不存机器和拓扑信息，几天后再看基本等于半废。因为你知道“数字是多少”，但不知道“这数字是在哪套环境下跑出来的”。
- `result.json` 和 `summary.json` 不能互相偷懒。单 case 会被单独拷走，summary 也会被单独拷走，所以两边都必须能独立自证实验条件。

### Function Explanation

- `collect_machine_info()`：用真实 Linux 命令采集机器信息，并在 `lscpu` 缺失时做最小回退。
- `build_cpu_allocation()`：把 `server_cpuset / wrk_cpuset / ai_proxy_cpuset` 转成统一的核数统计。
- `attach_benchmark_metadata()`：把 `experiment_context / artifacts` 直接嵌回主结果 JSON，而不是另外造平行 metadata 文件。
- `write_flamegraph_summary.py`：复用现有 `run-summary.log`，把 flamegraph 的 shell 变量整理成结构化 `run-summary.json`。

### Pitfalls

- 如果 metadata helper 不做缓存，像 Stage 1 搜索这种几十轮 case 的脚本会重复跑很多次 `lscpu`，虽然不是大问题，但完全没必要。
- 如果 flamegraph 只留 `run-summary.log` 不留 JSON，后面一旦要批量整理云机产物，就会又回到人工 grep 日志的老路。
- 如果 summary 级 JSON 不记录 `artifacts`，下载回本地后最容易发生的事就是“看到了聚合指标，却找不到对应的 SQLite / server.log / svg 在哪里”。

---

# 2026-04-17 feat(benchmark): 增加云机一键 benchmark campaign 入口

## Git Commit Message

`feat(benchmark): 增加云机一键 benchmark campaign 入口`

## Modification

- `server/tests/benchmark/run_paper_benchmark_cloud.sh`
- `server/tests/benchmark/run_paper_benchmark_cloud_unit_test.py`
- `server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh`
- `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `server/tests/benchmark/README.md`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

## Summary

- 新增总入口 `run_paper_benchmark_cloud.sh`，把当前已经冻结的 Suite A/B/D 云机实验串成一条命令：
  - Suite A `16` 核主叙事：`main tuned protected buffered` vs `cmp`
  - Suite A `16` 核 buffer 辅助归因：`buffered` vs `disable_buffered`
  - Suite B `16` 核正式 campaign：`5` seed、`3 x 2` 生命周期鲁棒性矩阵
  - Suite D `24` 核连接数确认搜索：默认 `90/108/120` 交错复跑
  - Suite D `24` 核主扩展曲线：默认用当前 gate tuned 点 `sweep=20 / max_dispatch=256`
- 总入口会自动读取 cgroup cpuset 起始核；如果云机只暴露 `160-191`，会自动把 Suite A/B/D 的 sender/backend cpuset 整体平移，不再手工改 `160-183`。
- 总入口支持 `--main-server-bin / --cmp-server-bin`，适配云机上主版本和历史版本来自两个不同 checkout 的情况。
- Suite B campaign stdout 默认落日志，终端只打印压缩摘要，避免云机结果复制时被大 JSON 刷屏。
- Suite D 新增 `run_suite_d_connection_search_24c.sh`，固定 `3/21` 拆分和当前 gate 参数，用中位数确认连接数冻结点。
- README 已补一键云机入口说明，Todo 已把连接数确认 wrapper 和总 campaign wrapper 标记完成。

## Verification

- `bash -n server/tests/benchmark/run_paper_benchmark_cloud.sh server/tests/benchmark/suite_d/run_suite_d_connection_search_24c.sh`
- `python3 -m unittest server.tests.benchmark.run_paper_benchmark_cloud_unit_test server.tests.benchmark.suite_d.suite_d_frozen_wrappers_unit_test`
- `python3 -m py_compile server/tests/benchmark/run_paper_benchmark_cloud_unit_test.py server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- `bash server/tests/benchmark/run_paper_benchmark_cloud.sh --dry-run --main-server-bin /tmp/main --cmp-server-bin /tmp/cmp --core-base-offset 160 --suite-b-seeds 1 --suite-d-repeats 1`

## Learning Tips

### Newbie Tips

- “一键脚本”不能重新定义实验参数。它只应该串联 frozen wrapper；否则过几天你会分不清论文结果到底来自单项脚本还是总脚本里的另一套参数。
- 云机容器的 `nproc=32` 不代表 cpuset 从 `0` 开始。真正能绑的核要看 cgroup，例如 `160-191`。

### Function Explanation

- `detect_core_base_offset()`：读取 `/sys/fs/cgroup/cpuset.cpus.effective` 或 v1 cpuset 文件，取第一个可用 CPU 作为整体平移基址。
- `build_cpuset()`：根据基址和核数生成 `160-163` 这种 `taskset` 可用的连续区间。
- `run_quiet_json()`：用于 Suite B 这种会打印大 JSON 的入口，把完整 stdout 写进 log，再从 summary JSON 摘几行关键摘要。

### Pitfalls

- 不要把 `Suite D flamegraph` 放进默认总入口。当前云机缺 `perf`，而 flamegraph 是解释图，不是主实验必跑项；默认塞进去只会增加失败点。
- 不要把 `Suite D topology search` 默认一起跑。它是前置校准项，不是每次主 campaign 都必须复跑；总入口保留 `--include-d-topology-search` 即可。

---

# 2026-04-17 fix(benchmark): 修正 Suite D drain 计时口径

## Git Commit Message

`fix(benchmark): 修正 Suite D drain 计时口径`

## Modification

- `server/tests/benchmark/suite_d/run_suite_d_case.py`
- `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

## Summary

- 修正 Suite D `drain_tail_ms` 的根因问题：
  - `t_stop_ms` 是 epoch 墙钟，适合写进结果 JSON 和 `server.log` 做时间线对齐；
  - `wait_until_sqlite_stable()` 内部用的是 `time.monotonic()`；
  - 之前把 epoch ms 传给 monotonic helper，会让差值变成巨大负数，再被 `max(0, ...)` 压成 `0`；
  - 所以云机上已跑出的 Suite D `drain=0` 不能作为真实尾巴结论引用。
- 现在 Suite D case 会在 wrk measurement 停发后同时记录两类时间：
  - `t_stop_ms = time.time() * 1000`，只用于结果留档；
  - `drain_start_ms = time.monotonic() * 1000`，只用于 drain 耗时计算。
- Suite D stdout 现在额外打印 `stop=x/offered` 和 `final=x/offered`，方便云机上直接判断：
  - 是窗口内没完成但最终追平；
  - 还是最终也没有追平；
  - 不需要立刻打开 `result.json` 才能排查。

## Verification

- 先写红灯测试：强制 `time.time()` 返回 `1776419859.941`，`time.monotonic()` 返回 `123.456`，确认旧代码会把 `start_ms` 错传成 `1776419859941`。
- `python3 -m unittest server.tests.benchmark.suite_d.run_suite_d_case_unit_test.SuiteDRunSuiteDCaseUnitTest.test_run_suite_d_case_writes_online_completion_and_drain_metrics`
- `python3 -m unittest server.tests.benchmark.suite_d.run_suite_d_case_unit_test`
- `python3 -m py_compile server/tests/benchmark/suite_d/run_suite_d_case.py server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`

## Learning Tips

### Newbie Tips

- 墙钟时间和耗时计时不能混用。墙钟会受系统时间影响，适合做日志对齐；`monotonic` 不倒退，适合算耗时。
- benchmark 指标如果出现“过于完美”的值，比如高压下所有点位 `drain=0`，要先怀疑计量口径，再拿它讲性能。

### Function Explanation

- `time.time()`：返回 epoch 秒数，适合写进结果文件做跨进程/跨日志对齐。
- `time.monotonic()`：返回单调递增时间，适合计算两个事件之间经过了多久。
- `wait_until_sqlite_stable()`：轮询 SQLite 是否追平目标 trace 数，并用传入的 `start_ms` 计算 drain tail。

### Pitfalls

- 不要把这次云机已经跑出的 Suite D `drain=0` 写进论文。修复前只有 `online / ratio / qps` 相对可参考，`drain` 必须复跑。
- CPU 没打满不等于没有瓶颈。更可能是 trace 完成路径里有串行闸门或单写链路，让空闲 CPU 没机会参与工作。
