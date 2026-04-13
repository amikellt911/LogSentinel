# CurrentTask

## 阶段信息
阶段：v1.0.0
开始日期：2026-04-10
归档日期：

## 当前阶段
v1.0.0：在 MVP5 已完成最小可演示闭环的基础上，继续把“真实生效、双 provider、可对比实验、可复现实验、可一键部署、可一键演示”这 6 件事收完，形成可打 tag 的正式版本。

## 版本目标
- [x] 完成 `Settings` 真实生效联调验收 (2026-04-12)
- [x] 完成前后端单入口部署：后端托管 `client/dist`，让 `http_port` 成为系统唯一入口端口 (2026-04-13)
- [x] 接入第 2 个真实 AI provider：`GLM` (2026-04-12)
- [x] 落地 AI 重试，让 `ai_retry_enabled / ai_retry_max_attempts` 从占位配置变成真实能力 (2026-04-13)
- [x] **配置热加载第一刀 (Hot-reloading)**: 支持主路与 fallback 的 `model/api_key` 在运行时无感切换；`ai_provider / ai_fallback_provider` 仍保持冷启动语义 (2026-04-13)
- [ ] 增加实验/对比开关，用于 benchmark 和论文对比
- [ ] 固定 benchmark 命令、场景、结果模板和截图
- [ ] 补 `Docker / docker-compose`，把轻量部署真正做成可复现能力
- [ ] 补一键演示启动脚本，服务于答辩和最终验收

## 本轮范围
要做：
- [ ] 把已经接进主链的 Settings 字段按“保存 -> 重启/生效 -> 前端可见变化”做一轮联调清单
- [ ] 收口前端正式设置入口与部署入口语义：
  - [x] `/settings` 只保留 `SettingsPrototype`
  - [x] 后端托管 `client/dist` 后只暴露单入口，不再要求用户记住前后端双端口
- [ ] 在现有 `mock / gemini` 之外接入 `GLM`，让自动降级真正覆盖两家真实 provider
- [ ] 在 Python proxy 落地 AI 重试：
  - [x] 真实消费 `ai_retry_enabled / ai_retry_max_attempts`
  - [x] 明确可重试错误范围
  - [x] 让重试结果与现有熔断 / 自动降级状态口径打通
- [ ] 增加最小实验开关：
  - [x] `disable_ai`
  - [x] `disable_buffered_trace_repo`
  - [x] `disable_webhook`
  - [x] 只服务 benchmark / 对比实验，不进正式产品设置页
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
- [x] Settings 第三层黑盒已覆盖关键冷启动配置：
  - [x] `http_port`
  - [x] `trace_end_aliases`
  - [x] `ai_analysis_enabled`
  - [x] `ai_retry_enabled / ai_retry_max_attempts`
  - [x] `prompt / active_prompt_id`
  - [x] `webhook channel (webhook_url / threshold / secret)`
  - [x] `kernel_worker_threads`
  - [x] `log_retention_days`
- [x] Trace AI 已具备 `skipped_manual / skipped_circuit / failed_primary / failed_both / completed` 最小可靠性骨架
- [x] 主 provider 失败后可自动尝试 fallback provider
- [x] Python AI proxy 已接入 `GLM` Trace provider 最小闭环：
  - [x] `chat/completions + response_format=json_object`
  - [x] 本地 JSON/schema 校验
  - [x] 统一 `usage / error_status / error_message` 协议
- [x] Python AI proxy 已接通 AI 重试主链：
  - [x] `timeout_ms` 以单个 provider 调用链总预算执行，不会在每次 attempt 重置
  - [x] `429 / 5xx / 网络错误` 可重试
  - [x] `PROVIDER_FORMAT_ERROR / PROVIDER_SCHEMA_ERROR / INVALID_PROVIDER_RESPONSE` 只额外补一枪
  - [x] `4xx` 鉴权/参数错误与 `TIMEOUT` 不重试
- [x] Trace AI 已完成热更新第一刀：
  - [x] 运行中修改 `ai_model / ai_api_key` 后，后续 trace 请求会带新值
  - [x] 运行中修改 `ai_fallback_model / ai_fallback_api_key` 后，后续降级请求会带新值
  - [x] `ai_provider` 仍然由启动期决定，不支持运行中切路由
  - [x] `ai_fallback_provider` 同样由启动期决定，不支持运行中切路由
  - [x] 热更新实现走“repo 版本号 + provider 本地小快照”语义，不在每次 AI 调用都全量重抓 Settings
- [x] 双 provider/fallback 黑盒已收口：
  - [x] `gemini` 主路成功
  - [x] `glm` 主路成功
  - [x] `gemini -> glm` 自动降级成功
  - [x] `glm -> gemini` 自动降级成功
  - [x] 主备都失败时落 `failed_both`
- [x] benchmark 最小 CLI 开关已接通第一刀：
  - [x] `--disable-ai` 会压过 SQLite 里的 `ai_analysis_enabled`
  - [x] `--disable-webhook` 会压过 Settings channel 与 CLI webhook override
  - [x] `--disable-buffered-trace-repo` 会把写入口从 BufferedTraceRepository 切到同步直写 SQLite
  - [x] 三条开关都已补第三层黑盒，验证优先级和真实消费链

## 核心任务
- [ ] 先做 Settings 真实生效联调，固化当前主链配置的验收口径
- [ ] 再做单入口部署，收口 `/settings` 与 `http_port` 的正式产品语义
- [x] 再接 `GLM`，补齐双真实 provider 能力
- [x] 再补 `GLM` 的端到端联调与 fallback 黑盒，真正收口双真实 provider
- [x] 再补 AI 重试，收口 Trace AI 可靠性链路
- [ ] 再补实验开关，服务 benchmark 和论文对比
- [ ] 再固定 benchmark 材料
- [ ] 最后做 Docker 和一键演示启动，收口发布形态

## 验收标准
- [ ] 能给出一份明确的 Settings 联调验收清单，并证明关键配置项真实生效
- [ ] 后端托管 `client/dist` 后，用户只需要访问一个端口，前端不再硬编码旧 API 端口
- [x] 后端托管 `client/dist` 后，用户只需要访问一个端口，前端不再硬编码旧 API 端口
- [x] `gemini + glm` 至少两家真实 provider 可用，自动降级链路可演示
- [x] `ai_retry_enabled / ai_retry_max_attempts` 已被真实消费，且重试结果与熔断 / 降级链路语义一致
- [ ] benchmark 命令、参数、结果表和截图可以直接复跑、直接放论文
- [ ] `docker-compose` 可以一条命令拉起最小演示环境
- [ ] 一键演示脚本可以服务答辩演示，不需要手工敲一堆命令
- [ ] 满足打 `v1.0.0` tag 和 release 的最低要求

## 备注
- `MVP5` 已归档到 `docs/archive/current-task/MVP5.md`。
- 当前论文初稿可以开写；v1.0.0 这轮新增内容主要补实验数据、部署复现能力和正式版本收尾材料。
- Settings 联调当前已经不是“只验保存回填”，而是已经补到真实黑盒消费验证；继续在 Settings 支线上追加更多黑盒的收益开始下降。
- 单入口部署已收口：
  - 后端支持 `--frontend-dist <path>` 显式指定前端静态目录，黑盒可喂临时 dist，不依赖本地先手工构建。
  - `/api/*` 会先剥前缀再走现有 Router，裸 API 仍兼容。
  - 非 API 请求会先尝试命中真实静态文件，再对白名单页面 `/ /service /traces /settings` fallback 到 `index.html`。
  - 未知路径如 `/fdasxz` 仍返回 404，不会误掉回前端壳页面。
- 这轮的重点不是再发明更多功能，而是把现有特色讲实、做实：
  - 轻量部署
  - 异常闭环
  - 低成本可用
- 单入口部署的目标不是“后端顺手拉起前端 dev server”，而是把交付态收口成“后端 API + 前端静态资源”同源访问。
- benchmark 实验开关当前先走 CLI，而不是进 Settings：
  - 目标是把“实验变量”跟“产品配置”拆开，避免为了做对照组去污染 SQLite 冷启动配置。
  - 当前最小三件套已完成：`--disable-ai`、`--disable-webhook`、`--disable-buffered-trace-repo`。
- 配置热更新当前只收了主路与 fallback 的 `model/api_key`：
  - 这是因为它们只是 proxy 请求体字段，适合运行时刷新。
  - `ai_provider / ai_fallback_provider` 会决定 `/analyze/trace/{provider}` 路由，仍然保持冷启动更稳。
