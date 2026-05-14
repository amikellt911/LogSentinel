# test(suite-d): 新增验收现场 mock 性能演示脚本

## Git Commit Message

`test(suite-d): 新增本机 mock 性能验收演示脚本`

## Modification

- 新增 `demo_suite_d_local4.sh`
- 更新 `docs/todo-list/Todo_Benchmark.md`

## Details

- 新增根目录脚本 `demo_suite_d_local4.sh`，用于验收现场一键启动 LogSentinel、打印前端地址、运行 Suite D mock AI wrk 压测，并在压测结束后保持后端存活。
- 脚本通过自身路径定位项目根目录，避免从不同目录执行时找错 `server/build/LogSentinel` 或 `trace_model_suite_d.lua`。
- 脚本会优先打印首页、Trace 查询、服务监控、设置页、SQLite DB、日志和结果 JSON 路径，再运行 warmup 与正式 measurement。
- 正式 measurement 结束后生成轻量 `result.json`，摘录 `qps / offered_traces / trace_summary / online_completed_traces_per_sec / completion_ratio / p95 / p99`。
- 压测结束后不主动杀 LogSentinel，只有 Ctrl+C 时触发清理，方便继续打开前端查看 Dashboard / TraceExplorer / ServiceMonitor。
- 根据本机 finalists 复跑结果，将默认参数回填为 `b031_balanced`：`worker_threads=12`、`dispatch_worker_threads=2`、`trace_active_session_limit=1024`、`trace_buffered_span_limit=8192`、`trace_max_dispatch_per_tick=128`。
- 选择 `b031_balanced` 而不是平均 trace/s 第一的 `b056_best_ratio`，是因为它完成比例更高、p95 更低，现场展示比刷峰值更稳。
- 新增 `DEMO_OBSERVE_BEFORE_WRK_SEC`，默认后端 ready 和地址打印后先等 `20s` 再跑 wrk，让浏览器先加载前端页面和基础 API。
- 根据 worker/proxy 搜索结果，将现场演示默认改为 `worker_threads=192`、`proxy_max_workers=192`，用于展示 AI 分析吞吐明显提升，同时保持主链 `trace_summary` 可见吞吐稳定。
- 演示脚本改为手动启动 Python proxy，并通过 `--trace-ai-base-url` 让后端连接本轮 proxy，避免 `--auto-start-proxy` 固定使用默认 `128` worker 造成口径不一致。
- 现场摘要 `result.json` 和终端输出新增 `trace_analysis` 数量、`ai_completed_traces_per_sec_after_wrk` 与 `ai_completion_ratio_after_wrk`。
- `run_wrk_once` 支持 `0 / 0s` duration 直接跳过，用于快速 smoke 时关闭 warmup；否则 wrk 会因为 `-d0s` 直接报 usage。

## Chinese Comments

- `demo_suite_d_local4.sh` 顶部注释说明脚本只服务验收现场，不替代正式 Suite D benchmark wrapper。
- `find_free_port` 附近补充中文注释，说明为什么自动选择端口，避免现场端口占用导致失败。
- `stop_backend` 附近补充中文注释，说明压测结束不清理进程、Ctrl+C 才清理的生命周期边界。
- `write_result_summary` 内嵌 Python 处补充中文注释，说明为什么只做现场轻量摘要，以及为什么从 wrk 日志取最后一段 measurement。
- 默认参数块补充中文注释，说明当前默认值来自 `b031_balanced`，并解释为什么优先选择综合稳定参数。
- 压测前等待窗口补充中文注释，说明同源前端和 wrk 共用同一个后端端口，立刻压测会挤占浏览器请求。
- proxy 生命周期管理处补充中文注释，说明手动启动 proxy 是为了控制 `--max-workers`，并且 Ctrl+C 时要和后端一起清理。
- SQLite 摘要读取处补充中文注释，说明 `trace_summary` 是主链可见口径，`trace_analysis` 才代表 AI 分析结果已经落库。
- `run_wrk_once` 的 `0s` 分支补充中文注释，说明为什么跳过该阶段而不是调用 wrk。

## Verification

- `bash -n demo_suite_d_local4.sh`
- `git diff --check`

## Learning Tips

### Newbie Tips

- 演示脚本和论文 benchmark 脚本不要混在一起改。前者追求现场可操作，后者追求结果口径干净。
- 如果脚本要从任意目录执行，不能依赖当前工作目录，应使用脚本自身路径反推项目根目录。

### Function Explanation

- `trap cleanup EXIT INT TERM`：保证脚本正常退出、Ctrl+C 或收到终止信号时都能清理子进程。
- `taskset -c`：把进程限制到指定 CPU 核心集合，减少 sender 和 backend 互相抢核带来的测试噪声。

### Pitfalls

- 同一个 wrk 日志里同时有 warmup 和 measurement 时，摘要必须取最后一段正式 measurement，否则会误把预热指标当成验收指标。
- 压测 runner 如果自动杀掉后端，就不适合拿来做前端演示，因为页面打开时数据源已经没了。
