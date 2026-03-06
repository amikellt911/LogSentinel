## 追加记录：冒烟脚本升级为 basic/advanced 双模式

## Git Commit Message
`test(trace): 冒烟脚本支持basic与advanced模式并补齐AI校验`

## Modification
- `server/tests/smoke_trace_spans.py`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 冒烟测试分层（basic/advanced）可以显著提升 CI 稳定性：基础链路与外部依赖链路分开失败、分开定位。
- 进程就绪判断不能只看端口可连，必须同时检测“被测进程是否仍存活”，否则容易误判为通过。

### Function Explanation
- `--mode basic|advanced`：通过同一脚本入口切换测试层级，减少重复维护成本。
- `start_server(..., enable_dev_deps=True)`：advanced 模式下追加 `--auto-start-deps`，让服务自动拉起 AI proxy 与 webhook mock。
- `wait_analysis_ready(...)`：轮询 `trace_analysis`，用于断言 AI 分析结果确实完成并落库。

### Pitfalls
- basic 与 advanced 并行跑且共用端口时会互相干扰，导致“把另一条测试的服务误当成本条就绪”。
- advanced 对 Python 运行环境有依赖（`fastapi/uvicorn`），依赖缺失时主服务会在启动阶段提前退出。
- 若不把“环境缺依赖”与“业务逻辑失败”区分记录，后续会把排障方向带偏。

---

## 追加记录：增强 advanced 诊断并修复 SQL 列兼容问题

## Git Commit Message
`fix(test): 增强advanced冒烟诊断并修复trace_analysis查询列错误`

## Modification
- `server/tests/smoke_trace_spans.py`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 当冒烟测试失败时，第一优先不是盲目延长超时，而是先提升可观测性（服务日志、DB 快照、依赖探测），这样定位会快很多。
- SQLite 表不一定有业务自增列 `id`，如果未显式建 `id`，查询“最新记录”应优先考虑 `rowid` 或明确时间字段。

### Function Explanation
- `read_available_process_logs(...)`：非阻塞拉取当前可读日志，避免服务仍在运行时调用 `read()` 卡住脚本。
- `print_db_snapshot(...)`：失败时打印 trace 相关表计数和最近记录，快速判断失败层级（分发/落库/分析）。
- `probe_proxy_and_webhook(...)`：对 8001/9999 做即时探测，区分“依赖不可用”与“业务链路异常”。

### Pitfalls
- 在 `wait_*` 轮询中吞掉 SQL 异常但不输出上下文，容易把“脚本查询写错”误判成“业务没落库”。
- 使用不存在的 `id` 列排序会触发持续异常重试，表现成“超时”，但根因其实是测试脚本本身。
- 只打印最终错误一句话，缺少快照信息时，排障过程会反复重跑且效率很低。