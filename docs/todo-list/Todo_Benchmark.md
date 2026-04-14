# Todo: Trace 性能与背压基准测试 (Benchmark)

## 1. 准备阶段 (Environment Preparation)
- [ ] 系统参数调优 (ulimit -n 65535, tcp_tw_reuse, somaxconn)
- [ ] 确保 Webhook Server 正常运行并准备接收告警
- [ ] 配置 AI Proxy (Python) 开启 Mock 模式并增加 20-50ms 延迟

## 2. 工具与脚本 (Scripting)
- [x] 编写通用 Trace 压测脚本 `server/tests/wrk/trace_model.lua`（支持 end/capacity/token/timeout/mixed 五种模型，模板按顺序回环）
- [x] 编写 Trace wrk benchmark 设计文档，收口模型目标、参数选择和第一版实验矩阵
- [x] 编写 benchmark suite 总览文档，先收口 Suite A/B/C 的比较目标、观测指标以及 CPU/线程这两类条件，不提前写死命令
- [x] 将 benchmark 结构改写为 `Suite A / Suite B / Suite D / Suite A-Interaction`，并明确 `Suite C` 暂不执行
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
- [x] 新增升级版 wrk Lua 脚本：每个 wrk 线程维护 active trace 池，而不是单一 current trace
- [x] 为 `run_flamegraph.sh` 增加脚本切换与 `TRACE_WRK_ROLE_PLAN / ACTIVE_POOL_SIZE` 透传，支持 active-pool 流量模型
- [ ] 为升级版脚本补最小可复现参数和运行说明（优先 timeout/late-span 场景）
- [ ] 跑最小对照实验：旧脚本 vs 升级版脚本，确认请求分布和复现能力差异
- [x] 固定 `Suite D` 的最小资源轴口径：`D1 固定 CPU 扫 worker`、`D2 固定 worker 扫 CPU`
- [ ] 固定 `Suite A-Interaction` 的最小交互矩阵：优先 `buffered vs no-buffer`
- [x] 为 Python AI proxy 补 `--max-workers` 红灯测试，锁定 CLI 参数与 AnyIO 默认线程 limiter 真正联动
- [x] 在 `server/ai/proxy/main.py` 接通 `--max-workers`，并补中文注释说明这是 AI 代理层并发上限，不是厂商配额承诺
- [x] 运行 Python proxy 最小单测验证，再决定是否把这个变量补进 benchmark 文档口径
- [x] 将 `ai_proxy_max_workers` 正式收进 benchmark 文档口径，并和 `backend_cpu_cores / server_io_threads / worker_threads / ai_proxy_cpu_cores` 区分开
- [x] 固定 `Suite D` 下 `ai_proxy_max_workers` 的最小扫描点位（优先 `64 / 128 / 256 / 512`）
- [x] 固定 `trace_lifecycle_profile=protected|minimal` 的 benchmark 实验入口口径
- [x] 让 `run_bench.sh / run_flamegraph.sh` 透传 `TRACE_LIFECYCLE_PROFILE`
- [x] 补黑盒验证：CLI `--trace-lifecycle-profile` 必须盖过 SQLite 冷启动值，并锁定真实生命周期差异
- [x] 新增最小生命周期测试脚本，只覆盖 `trace_end -> 晚到 span` 单场景，不承担正式 benchmark 压流职责
- [x] 把 `dispatch / worker / io / flush` 的真实职责写进 benchmark 文档，避免后续再把热路径说反
- [x] 记录 `dispatch_tpool` 的真实前置条件：当前 `ThreadPool(std::function<void()>)` 不能直接承载 move-only `DispatchJob`

## 3. 代码改造 (Main Enhancement)
- [x] 为 `dispatch_worker_threads` 补最小黑盒，先锁冷启动消费和启动日志口径
- [x] 在 `AppConfig / SqliteConfigRepository / main.cpp / TraceSessionManager` 接通 `dispatch_worker_threads`
- [x] 把单 `dispatch_thread_` 改成可配置多 consumer dispatch 线程组，并补中文注释说明所有权和停机顺序
- [x] 让 benchmark CLI 支持 `--dispatch-worker-threads`，避免做实验时只能先改 SQLite
- [x] 运行 Trace 相关单测与黑盒，确认功能没回归
- [x] 增加 `--worker-threads` 命令行参数支持
- [x] 增加 `--trace-buffered-span-limit` 命令行参数支持
- [x] 增加 `--worker-queue-size` 命令行参数支持
- [x] 增加 `--trace-capacity` / `--trace-token-limit` / `--trace-max-dispatch-per-tick` 参数支持
- [x] 验证配置参数能准确透传给 `ThreadPool` 和 `TraceSessionManager`
- [x] 增加 `--trace-lifecycle-profile protected|minimal` 命令行参数支持

## 4. 压力测试 (Execution)
- [ ] **Suite A 主实验**: 固定资源，只跑功能开关成本对比
- [ ] **Suite B 主实验**: 固定资源，只跑 Trace 生命周期鲁棒性对比
- [ ] **Suite D1 扩展性实验**: 固定 CPU，只扫 `kernel_worker_threads`
- [ ] **Suite D2 扩展性实验**: 固定 worker，只扫后端可用 CPU
- [ ] **Suite A-Interaction 小交互实验**: 少量资源点下比较 `baseline / disable_buffered_trace_repo / no_ai_no_buffer`
- [ ] **摸底测试 (Baseline)**: `-c 100`, 验证 QPS 指标
- [ ] **极限测试 (Saturation)**: `-c 500`, 寻找 P99 拐点
- [ ] **背压验证 (Backpressure)**: `-c 2000`, 验证 503 拦截生效与内存稳定性
- [ ] **恢复测试 (Recovery)**: 观察积压清空后的状态自动回落

## 5. 战果记录 (Reporting)
- [ ] 整理 QPS/P99 数据表格
- [ ] 生成背压介入时的截图或记录
- [ ] 记录 CPU 核心隔离下的性能表现
- [x] 追加 dev-log 完成复盘
