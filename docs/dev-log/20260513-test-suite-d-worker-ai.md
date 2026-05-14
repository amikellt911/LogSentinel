# test(suite-d): 新增 worker/proxy 并发搜索脚本

## Git Commit Message

`test(suite-d): 新增 worker 与 AI proxy 并发搜索脚本`

## Modification

- 新增 `search_suite_d_worker_ai.py`
- 更新 `server/tests/benchmark/suite_d/run_suite_d_case.py`
- 更新 `demo_suite_d_local4.sh`
- 更新 `docs/todo-list/Todo_Benchmark.md`
- 更新 `docs/dev-log/20260513-test-suite-d-demo.md`

## Details

- 新增根目录脚本 `search_suite_d_worker_ai.py`，用于搜索 Suite D AI-on mock 场景下 `kernel_worker_threads` 和 Python proxy `--max-workers` 的关系。
- 脚本每个 case 手动启动一份 Python AI proxy，并通过 `--trace-ai-base-url` 让 C++ 后端连接本轮 proxy，避免复用默认 `8001` 或默认 `128` worker 影响结论。
- 默认 `proxy_max_workers = ceil(worker_threads * proxy_scale)`，并通过 `--proxy-max-workers-cap` 封顶，方便从 `12` 一路扫到 `256`。
- 默认每轮结束删除 SQLite DB，只保留 result/server/proxy 日志和 summary，避免长搜索把 `/tmp` 写满。
- `run_suite_d_case.py` 增加 `--trace-ai-base-url` 透传能力，让单 case runner 可以服务手动 proxy 场景。
- 演示脚本 `DEMO_OBSERVE_BEFORE_WRK_SEC` 默认值从 `15s` 调整为 `20s`，给现场前端页面更多加载时间。
- `run_suite_d_case.py` 新增 `trace_analysis` 计数，输出 `final_trace_analysis_count / online_ai_completed_traces_per_sec / final_ai_completion_ratio / final_ai_offered_ratio`。
- `search_suite_d_worker_ai.py` 的单行结果和 aggregate top 增加 `ai_online / ai_final`，并把排序第一优先级改成 AI 分析完成吞吐，避免只按 `trace_summary` 主链可见量误判 worker/proxy 搜索结果。

## Chinese Comments

- `search_suite_d_worker_ai.py` 的 `sys.path` 注释说明为什么复用 Suite D 单 case runner，同时把 proxy 生命周期单独拿出来控制。
- `find_free_port` 注释说明 server/proxy 端口都由脚本分配，避免撞旧进程或误连默认 proxy。
- `start_proxy` 注释说明为什么每个 worker 点都显式启动 proxy 并设置 `--max-workers`。
- `cleanup_case_sqlite_db` 注释说明为什么默认删除 DB，避免搜索过程撑爆 `/tmp`。
- `run_suite_d_case.py` 的 `--trace-ai-base-url` 透传处补充中文注释，说明外置 proxy 场景不能只关闭 auto-start，还必须绑定本轮 proxy 地址。
- `run_suite_d_case.py` 的 `read_trace_analysis_count` 附近补充中文注释，说明 `trace_summary` 是主链可见口径，`trace_analysis` 才是 AI 分析完成口径。
- `search_suite_d_worker_ai.py` 的聚合排序处补充中文注释，说明 AI-on 搜索必须优先看 `trace_analysis`，否则会被主链吞吐误导。
- `demo_suite_d_local4.sh` 的等待窗口注释补充说明 20s 只影响演示体验，不进入 benchmark 计时口径。

## Verification

- `python3 -m py_compile search_suite_d_worker_ai.py server/tests/benchmark/suite_d/run_suite_d_case.py`
- `python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- `bash -n demo_suite_d_local4.sh`
- `./search_suite_d_worker_ai.py --dry-run --worker-points 12,16 --repeats 2 --summary-json /tmp/suite_d_worker_ai_dryrun.json --run-root /tmp/suite_d_worker_ai_dryrun`
- `./search_suite_d_worker_ai.py --worker-points 12 --repeats 1 --duration 1s --warmup-duration 0s --connections 2 --summary-json /tmp/suite_d_worker_ai_smoke.json --run-root /tmp/suite_d_worker_ai_smoke --cooldown-sec 0`
- 本轮 smoke 的 `sqlite_db_policy=delete_after_case`，且 `/tmp/suite_d_worker_ai_smoke-20260513-211836-093ms` 下无残留 `.db` 文件。
- `./search_suite_d_worker_ai.py --worker-points 12 --repeats 1 --duration 1s --warmup-duration 0s --connections 2 --summary-json /tmp/suite_d_worker_ai_analysis_smoke.json --run-root /tmp/suite_d_worker_ai_analysis_smoke --cooldown-sec 0`
- 新增 AI 指标 smoke 输出 `ai_online=12.00`、`ai_final=0.0175`，且 `/tmp/suite_d_worker_ai_analysis_smoke-20260513-213934-020ms` 下无残留 `.db` 文件。

## Learning Tips

### Newbie Tips

- 搜索 C++ worker 时不能忘了 Python proxy 的并发上限。否则 C++ worker 加到很大，proxy 仍可能被默认 limiter 卡住，结论会被截断。
- `--no-auto-start-proxy` 只表示后端不要自动拉 sidecar，不等于关闭 AI；如果要复用手动 proxy，还必须同时传 `--trace-ai-base-url`。
- `trace_summary` 和 `trace_analysis` 不是同一个完成阶段。前者说明 Trace 主体可见，后者才说明 AI 分析结果已经写入数据库。

### Function Explanation

- `subprocess.Popen`：用于启动独立 proxy 进程，脚本保存 `Popen` 对象后才能在 case 结束时可靠终止。
- `socket.connect_ex`：用于探测端口是否已被监听，返回 `0` 表示连接成功，非 `0` 表示端口当前不可连接。

### Pitfalls

- 如果搜索脚本不按 case 删除 SQLite DB，长时间多轮搜索会很快占用大量磁盘空间。
- 如果失败行不记录 `proxy_log` 和端口，后续只能重新跑才能定位问题，排查成本会很高。
