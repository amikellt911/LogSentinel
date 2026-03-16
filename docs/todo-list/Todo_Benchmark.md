# Todo: Trace 性能与背压基准测试 (Benchmark)

## 1. 准备阶段 (Environment Preparation)
- [ ] 系统参数调优 (ulimit -n 65535, tcp_tw_reuse, somaxconn)
- [ ] 确保 Webhook Server 正常运行并准备接收告警
- [ ] 配置 AI Proxy (Python) 开启 Mock 模式并增加 20-50ms 延迟

## 2. 工具与脚本 (Scripting)
- [x] 编写通用 Trace 压测脚本 `server/tests/wrk/trace_model.lua`（支持 end/capacity/token/timeout/mixed 五种模型，模板按顺序回环）
- [x] 编写 Trace wrk benchmark 设计文档，收口模型目标、参数选择和第一版实验矩阵
- [x] 编写阶段性性能实验模板 `docs/PERFORMANCE_PHASE_TEMPLATE.md`，固定论文可复用的命令、指标与记录格式
- [x] 编写一键压测编排脚本 `server/tests/wrk/run_bench.sh`（最小版：起服务、等端口、跑 wrk、双写日志、自动 cleanup）
- [x] 编写火焰图编排脚本 `server/tests/wrk/run_flamegraph.sh`（最小版：起服务、warmup、perf record、正式 wrk、生成 svg、自动 cleanup）
- [x] 编写低速率 Trace 发送脚本 `server/tests/wrk/trace_paced_sender.py`（最小版：按 batch-traces + batch-sleep-ms 固定节奏发送 end trace）
- [x] 为火焰图实验补充后链路 `drain` 等待与最终落库统计（避免只看到入口成功，不知道最后真正完成多少 trace）
- [x] 为 Trace 后链路补最小埋点（Dispatch/submit/worker/AI/SQLite 的次数与累计墙钟耗时）
- [x] 修正 Trace/BufferedTrace 埋点口径：把 manager 内的 analysis enqueue 和后台 flush 线程里的真实 SQLite batch flush 分开统计
- [x] 为 `run_bench.sh` 自动摘出 `[TraceRuntimeStats] / [BufferedTraceRuntimeStats]`，避免每轮 benchmark 后手翻 server log
- [x] 为 `run_bench.sh` 补受控停机与 shutdown snapshot，避免 worker/AI 积压太深时脚本一直卡在退出阶段
- [x] 为 `SavePrimaryBatch` 补失败日志，输出 batch 边界 trace_id 和具体 SQLite 错误，避免 flush 失败时只看到 fail_count

## 3. 代码改造 (Main Enhancement)
- [x] 增加 `--worker-threads` 命令行参数支持
- [x] 增加 `--trace-buffered-span-limit` 命令行参数支持
- [x] 增加 `--worker-queue-size` 命令行参数支持
- [x] 增加 `--trace-capacity` / `--trace-token-limit` / `--trace-max-dispatch-per-tick` 参数支持
- [x] 验证配置参数能准确透传给 `ThreadPool` 和 `TraceSessionManager`

## 4. 压力测试 (Execution)
- [ ] **摸底测试 (Baseline)**: `-c 100`, 验证 QPS 指标
- [ ] **极限测试 (Saturation)**: `-c 500`, 寻找 P99 拐点
- [ ] **背压验证 (Backpressure)**: `-c 2000`, 验证 503 拦截生效与内存稳定性
- [ ] **恢复测试 (Recovery)**: 观察积压清空后的状态自动回落

## 5. 战果记录 (Reporting)
- [ ] 整理 QPS/P99 数据表格
- [ ] 生成背压介入时的截图或记录
- [ ] 记录 CPU 核心隔离下的性能表现
- [ ] 追加 dev-log 完成复盘
