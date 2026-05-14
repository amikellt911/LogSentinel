# test(suite-d): 调整验收脚本展示 AI 分析数量

## Git Commit Message

`test(suite-d): 调整验收脚本展示 AI 分析数量`

## Modification

- 更新 `demo_suite_d_local4.sh`
- 更新 `docs/todo-list/Todo_Benchmark.md`
- 更新 `docs/dev-log/20260513-test-suite-d-demo.md`

## Details

- 将验收现场脚本默认参数调整为 `worker_threads=192`、`proxy_max_workers=192`，对齐 worker/proxy 搜索中更适合展示 AI 分析吞吐的配置。
- 演示脚本改为手动启动 Python proxy，再通过 `--trace-ai-base-url` 让后端连接本轮 proxy，避免 `--auto-start-proxy` 固定默认 `128` worker 影响演示口径。
- 终端和 `result.json` 新增 `trace_analysis` 数量、`ai_completed_traces_per_sec_after_wrk` 和 `ai_completion_ratio_after_wrk`。
- `run_wrk_once` 支持 `0 / 0s` duration 直接跳过，方便 smoke 时关闭 warmup。
- 修正 Dashboard 切回页面不主动刷新问题：`TraceExplorer` 会在 `onMounted` 拉列表，而 `Dashboard` 原来只初始化图表、依赖全局轮询，所以路由切回来时容易继续显示旧快照。
- 为 `TraceExplorer` 增加 2s 轻量轮询：页面挂载后自动补拉列表，离开页面自动停止，避免必须切到别的页面再回来才能看到新数据。

## Chinese Comments

- 默认参数块补充中文注释，说明为什么使用 `worker=192 / proxy=192`，以及为什么不能只改 C++ worker。
- proxy 生命周期管理处补充中文注释，说明手动 proxy 用于控制 `--max-workers`，并且 Ctrl+C 时需要和后端一起清理。
- SQLite 摘要读取处补充中文注释，说明 `trace_summary` 代表主链可见，`trace_analysis` 才代表 AI 分析结果落库。
- `run_wrk_once` 的 `0s` 分支补充中文注释，说明 wrk 不接受 `-d0s`，所以脚本应跳过该阶段。
- `client/src/stores/system.ts` 暴露 `fetchDashboardStats` 处补充中文注释，说明 Dashboard 切回时需要主动补拉快照。
- `client/src/views/Dashboard.vue` 的 `onMounted` 处补充中文注释，说明不能只依赖全局轮询，切回页面时要主动刷新。
- `client/src/views/TraceExplorer.vue` 增加轮询函数与 `onUnmounted` 清理，补充中文注释说明列表刷新不再只依赖路由切换。

## Verification

- `bash -n demo_suite_d_local4.sh`
- `DEMO_OBSERVE_BEFORE_WRK_SEC=0 WARMUP_DURATION=0s DURATION=1s CONNECTIONS=2 WRK_THREADS=1 timeout 12s ./demo_suite_d_local4.sh`
- smoke 输出包含 `sqlite_trace_analysis_after_wrk=128`、`ai_completed_traces_per_sec_after_wrk=128.00`、`ai_completion_ratio_after_wrk=0.2370`。
- smoke 最终返回 `124` 是预期行为，因为脚本压测后会保持后端运行等待 Ctrl+C，本次由 `timeout` 强制结束。
- `cd client && npm run build`
- `client/src/views/TraceExplorer.vue` 已通过 `npm run build` 验证，轮询补丁没有破坏 Vue/TS 类型。

## Learning Tips

### Newbie Tips

- 演示脚本如果要复现搜索脚本的 `worker=192 / proxy=192`，必须同时控制 C++ worker 和 Python proxy worker。
- 只打印 `trace_summary` 容易误导，因为它不等于 AI 分析完成。
- Vue 页面切回是否刷新，不要靠“感觉组件还在”。要看具体页面有没有在 `onMounted` 或路由激活时主动请求后端。

### Function Explanation

- `timeout 12s ./demo_suite_d_local4.sh`：用于 smoke 这种需要自动结束的场景；真实演示不需要加 `timeout`。
- `--trace-ai-base-url`：让后端连接指定的外部 proxy，而不是使用默认自动拉起的 proxy。
- `onMounted`：组件挂载后执行的生命周期钩子，适合页面进入时补拉当前快照。

### Pitfalls

- wrk 不接受 `-d0s`，所以 `WARMUP_DURATION=0s` 必须在脚本里跳过，而不是继续调用 wrk。
- 如果只改 `WORKER_THREADS=192` 但 proxy 仍是默认 `128`，AI 吞吐会被 proxy limiter 截断。
