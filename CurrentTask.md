# CurrentTask

## 阶段信息
阶段：v1.0.0
开始日期：2026-04-10
归档日期：

## 当前阶段
v1.0.0：在 MVP5 已完成最小可演示闭环的基础上，继续把“真实生效、双 provider、可对比实验、可复现实验、可一键部署、可一键演示”这 6 件事收完，形成可打 tag 的正式版本。

## 版本目标
- [ ] 完成 `Settings` 真实生效联调验收
- [ ] 接入第 2 个真实 AI provider：`GLM`
- [ ] 落地 AI 重试，让 `ai_retry_enabled / ai_retry_max_attempts` 从占位配置变成真实能力
- [ ] 增加实验/对比开关，用于 benchmark 和论文对比
- [ ] 固定 benchmark 命令、场景、结果模板和截图
- [ ] 补 `Docker / docker-compose`，把轻量部署真正做成可复现能力
- [ ] 补一键演示启动脚本，服务于答辩和最终验收

## 本轮范围
要做：
- [ ] 把已经接进主链的 Settings 字段按“保存 -> 重启/生效 -> 前端可见变化”做一轮联调清单
- [ ] 在现有 `mock / gemini` 之外接入 `GLM`，让自动降级真正覆盖两家真实 provider
- [ ] 在 Python proxy 落地 AI 重试：
  - [ ] 真实消费 `ai_retry_enabled / ai_retry_max_attempts`
  - [ ] 明确可重试错误范围
  - [ ] 让重试结果与现有熔断 / 自动降级状态口径打通
- [ ] 增加最小实验开关：
  - [ ] `disable_ai`
  - [ ] `disable_buffered_trace_repo`
  - [ ] `disable_webhook`
  - [ ] 只服务 benchmark / 对比实验，不进正式产品设置页
- [ ] 固定 benchmark：
  - [ ] 压测命令
  - [ ] 参数矩阵
  - [ ] 结果模板
  - [ ] 论文截图
- [ ] 补 `docker-compose`，至少覆盖前端、后端、AI proxy
- [ ] 补一键演示脚本，例如 `run_demo.sh`

不做：
- [ ] 不做新一轮大架构重写
- [ ] 不做 Redis / RocksDB / 分布式改造
- [ ] 不做完整 observability 平台化扩张
- [ ] 不把 benchmark 实验开关强行做进正式 Settings 产品语义
- [ ] 不把“后端自动拉前端”塞进 C++ 主进程生命周期里

## 当前基线
- [x] Trace 主链已经稳定：`/logs/spans -> TraceSessionManager -> BufferedTraceRepository -> SqliteTraceRepository`
- [x] Trace 读侧、详情、AI 分析、瀑布图已经接通真实数据
- [x] `Dashboard / ServiceMonitor` 已接真值
- [x] 飞书 Webhook 已完成真实联调
- [x] Settings 已接通一批关键字段真实消费
- [x] Trace AI 已具备 `skipped_manual / skipped_circuit / failed_primary / failed_both / completed` 最小可靠性骨架
- [x] 主 provider 失败后可自动尝试 fallback provider

## 核心任务
- [ ] 先做 Settings 真实生效联调，固化当前主链配置的验收口径
- [ ] 再接 `GLM`，补齐双真实 provider 能力
- [ ] 再补 AI 重试，收口 Trace AI 可靠性链路
- [ ] 再补实验开关，服务 benchmark 和论文对比
- [ ] 再固定 benchmark 材料
- [ ] 最后做 Docker 和一键演示启动，收口发布形态

## 验收标准
- [ ] 能给出一份明确的 Settings 联调验收清单，并证明关键配置项真实生效
- [ ] `gemini + glm` 至少两家真实 provider 可用，自动降级链路可演示
- [ ] `ai_retry_enabled / ai_retry_max_attempts` 已被真实消费，且重试结果与熔断 / 降级链路语义一致
- [ ] benchmark 命令、参数、结果表和截图可以直接复跑、直接放论文
- [ ] `docker-compose` 可以一条命令拉起最小演示环境
- [ ] 一键演示脚本可以服务答辩演示，不需要手工敲一堆命令
- [ ] 满足打 `v1.0.0` tag 和 release 的最低要求

## 备注
- `MVP5` 已归档到 `docs/archive/current-task/MVP5.md`。
- 当前论文初稿可以开写；v1.0.0 这轮新增内容主要补实验数据、部署复现能力和正式版本收尾材料。
- 这轮的重点不是再发明更多功能，而是把现有特色讲实、做实：
  - 轻量部署
  - 异常闭环
  - 低成本可用
