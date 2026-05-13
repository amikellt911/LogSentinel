# test(suite-d): 新增本机 mock 参数搜索脚本

## Git Commit Message

`test(suite-d): 新增本机 mock 参数搜索脚本`

## Modification

- 新增 `search_suite_d_demo_params.py`
- 更新 `docs/todo-list/Todo_Benchmark.md`

## Details

- 新增根目录脚本 `search_suite_d_demo_params.py`，用于本机 `4` 核 mock AI 场景下批量搜索 Suite D 演示参数。
- 脚本复用 `server/tests/benchmark/suite_d/run_suite_d_case.py`，避免重复实现后端起停、wrk 解析和 SQLite drain 口径。
- 当前支持 `quick / balanced / wide` 三档候选集合，默认 `quick` 只跑 9 个围绕现场脚本默认值的小范围扰动候选。
- 排序口径先看 `final_completion_ratio`，再看 `online_completion_ratio` 和 `online_completed_traces_per_sec`，避免把“入口 QPS 高但 trace 大量积压”的参数误判成最优。
- 默认每个 case 提取指标后删除 SQLite DB，只保留 `result.json / server.log / summary.json`，避免多轮搜索占满磁盘；如需保留 DB，可显式传 `--keep-sqlite-db`。

## Quick Search Result

- 本轮命令：`python3 search_suite_d_demo_params.py --preset quick --summary-json /tmp/suite_d_param_search_quick.json --run-root /tmp/suite_d_param_search_quick --duration 8s --warmup-duration 2s --cooldown-sec 0.5`
- 当前 Top 1：`flush1024`
- 当前 Top 1 参数：`connections=30, worker_threads=8, dispatch_worker_threads=1, trace_primary_flush_span_threshold=1024, trace_primary_flush_interval_ms=5, trace_sweep_interval_ms=200, trace_max_dispatch_per_tick=64`
- 对比 baseline：`final_completion_ratio` 从 `0.4118` 提升到 `0.4569`。
- 空间确认：`/tmp/suite_d_param_search_quick.json` 中 9 个 case 的 SQLite DB 均已删除。

## Finalists Rerun Result

- 本轮命令：`python3 search_suite_d_demo_params.py --preset finalists --repeats 3 --summary-json /tmp/suite_d_param_finalists_r3.json --run-root /tmp/suite_d_param_finalists_r3 --duration 8s --warmup-duration 2s --cooldown-sec 0.5`
- 当前平均 `online_completed_traces_per_sec` 第一：`b056_best_ratio`
- `b056_best_ratio`：`online_avg=479.04, online_std=7.03, final_avg=0.4837, p95_avg=8.37ms`
- 更适合现场稳定展示的候选：`b031_balanced` 或 `b017_traceps_low_p95`
- `b031_balanced`：`online_avg=471.46, online_std=9.72, final_avg=0.5198, p95_avg=6.65ms`
- `b017_traceps_low_p95`：`online_avg=470.75, online_std=15.69, final_avg=0.5195, p95_avg=5.91ms`
- 空间确认：`/tmp/suite_d_param_finalists_r3.json` 中 15 个 case 的 SQLite DB 均已删除。

## Chinese Comments

- `search_suite_d_demo_params.py` 顶部注释说明脚本复用 Suite D 单 case runner，自己只负责参数组合和排序。
- `find_free_port` 注释说明为什么要跳过旧进程占用端口和本轮已用端口。
- `build_quick_candidates` 注释说明 quick 档不是暴力全局搜索，而是围绕当前 demo 默认参数做小范围扰动。
- `rank_key` 注释说明为什么先看最终完成比例，再看窗口内完成比例和吞吐。
- `cleanup_case_sqlite_db` 注释说明为什么默认删除 SQLite DB，防止多轮搜索占满磁盘。
- `build_finalist_candidates` 注释说明 finalists 档来自 balanced 粗筛优秀候选，用于多轮复跑排除偶然性。
- `aggregate_candidate_rows` 注释和字段命名说明多轮均值、最小值、最大值与标准差的统计口径。

## Verification

- `python3 -m py_compile search_suite_d_demo_params.py`
- `python3 search_suite_d_demo_params.py --dry-run --preset quick --summary-json /tmp/suite_d_param_search_dry.json`
- `python3 search_suite_d_demo_params.py --preset quick --max-cases 1 --duration 1s --warmup-duration 0s --summary-json /tmp/suite_d_param_search_smoke.json --run-root /tmp/suite_d_param_search_smoke`
- `python3 search_suite_d_demo_params.py --preset quick --summary-json /tmp/suite_d_param_search_quick.json --run-root /tmp/suite_d_param_search_quick --duration 8s --warmup-duration 2s --cooldown-sec 0.5`
- `python3 search_suite_d_demo_params.py --preset finalists --repeats 3 --dry-run --summary-json /tmp/suite_d_param_finalists_dry.json`
- `python3 search_suite_d_demo_params.py --preset finalists --repeats 3 --summary-json /tmp/suite_d_param_finalists_r3.json --run-root /tmp/suite_d_param_finalists_r3 --duration 8s --warmup-duration 2s --cooldown-sec 0.5`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 参数搜索不能只看 `Requests/sec`，因为入口请求多不代表 Trace 聚合、存储和 AI 分析真的完成了。
- 搜索脚本要默认清理大体积 DB 文件，否则连续跑几轮很容易把 `/tmp` 或工作盘打满。

### Function Explanation

- `itertools.product`：生成多维参数笛卡尔积，用于 balanced/wide 档批量组合候选。
- `argparse`：把搜索档位、运行时长、端口范围、是否保留 DB 等运行参数暴露成 CLI。

### Pitfalls

- 如果 warmup 和 measurement 混在一起解析，结果会不稳定；这里复用 `run_suite_d_case.py` 避免重复踩这个坑。
- 如果按 QPS 排序，会倾向选入口更猛但后台完成更差的参数，因此排序必须优先看 trace 级完成比例。
