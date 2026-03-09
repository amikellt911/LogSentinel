# Todo: Trace 性能与背压基准测试 (Benchmark)

## 1. 准备阶段 (Environment Preparation)
- [ ] 系统参数调优 (ulimit -n 65535, tcp_tw_reuse, somaxconn)
- [ ] 确保 Webhook Server 正常运行并准备接收告警
- [ ] 配置 AI Proxy (Python) 开启 Mock 模式并增加 20-50ms 延迟

## 2. 工具与脚本 (Scripting)
- [x] 编写通用 Trace 压测脚本 `server/tests/wrk/trace_model.lua`（支持 end/capacity/token/timeout/mixed 五种模型，模板按顺序回环）
- [x] 编写 Trace wrk benchmark 设计文档，收口模型目标、参数选择和第一版实验矩阵
- [x] 编写一键压测编排脚本 `server/tests/wrk/run_bench.sh`（最小版：起服务、等端口、跑 wrk、双写日志、自动 cleanup）

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
