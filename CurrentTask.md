# CurrentTask

## 阶段信息
阶段：MVP5
开始日期：2026-03-17
归档日期：

## 当前阶段
MVP5：以前后端联调和答辩交付为目标，补齐 Trace 可查询、运行态可观测、Webhook 真实外发、配置项真实生效这四条最小闭环，并同步沉淀 benchmark 结果用于论文与答辩材料。

## 阶段目标
- [x] 打通 Trace 读侧闭环，让前端能真实查询 trace 列表、详情、调用链与 AI 分析
- [ ] 把 Dashboard / ServiceMonitor 的核心展示区域接到真实后端运行时指标，而不是继续依赖 mock
- [ ] 打通至少 1 个真实外部告警渠道，保证 critical trace 能真实外发（本轮先收口飞书 webhook）
- [ ] 收口 Settings 语义，确保页面上的配置项要么真实生效，要么明确标注“不在本轮生效”

## 本轮范围
要做：
- [x] 后端补 Trace 查询接口：列表分页、按 trace_id 查详情、返回 spans / analysis
- [x] 前端 `TraceExplorer` 替换当前 mock 数据，接入真实 API
- [ ] 暴露 Trace 主链路 runtime stats 接口，覆盖 `TraceSessionManager` 与 `BufferedTraceRepository` 关键指标
- [ ] 前端 `Dashboard` / `ServiceMonitor` 至少接通一批核心真实指标
- [ ] 收口 Webhook 告警链路：至少支持 1 个真实外部平台（先飞书），能把 critical trace 发到真实群机器人
- [ ] 梳理 Settings 页面字段，去掉假重启、假生效或误导性语义
- [ ] 固定 benchmark 命令、参数、结果模板，沉淀论文可直接引用的数据和截图

不做：
- [ ] 不做新一轮高并发极限优化
- [ ] 不做完整监控系统或 Redis / 缓存扩展
- [ ] 不做 `Benchmark` 页面真联调，benchmark 结果以脚本和文档为主
- [ ] 不做 `History` / `LiveLogs` 老链路大修
- [ ] 不做跨进程去重、分布式配置热更新、主线程模型重构
- [ ] 不做飞书 / 钉钉双平台同时打磨；本轮先把飞书 webhook 做成稳定演示链路，钉钉后置
- [ ] 不做 Trace `prompt_debug` 真联调；当前主链路没有稳定产出该数据，后续若仍无真实来源，前端入口可直接删除
- [ ] 不做 Trace 列表“按耗时搜索/过滤”能力；当前仅保留耗时展示，不把 `duration` 作为首批读侧查询条件（原因：`/logs/spans` 的耗时依赖 `end_time_ms` 上报完整性，拿它做筛选容易误导）

## 核心任务
- [x] 先补 `SqliteTraceRepository + Handler + Router` 的 Trace 读侧能力，形成最小可查询后端接口
- [x] 再接通 `TraceExplorer`，把列表、详情抽屉、调用链、AI 分析切到真实数据
- [ ] 再把 runtime stats 暴露给前端，收口系统状态与背压展示
- [ ] 再把 critical trace -> webhook 外发链路收口到真实平台（先飞书），形成可演示告警闭环
- [ ] 最后清理 Settings 的假能力，并完成 benchmark 结果归档与联调验收

## 验收标准
- [x] `TraceExplorer` 能真实看到 trace 列表与详情，而不是 mock 数据
- [ ] `Dashboard` / `ServiceMonitor` 至少一批关键卡片来自真实后端指标
- [ ] 至少有 1 次真实 Webhook 联调记录：critical trace 产生后，飞书群机器人能收到结构化告警消息
- [ ] `Settings` 页面不再出现“能保存但当前主链路根本不吃”的假功能误导
- [ ] 有一套可复跑的 benchmark 命令、结果记录和截图，可直接用于论文与答辩
- [ ] 有一次完整联调记录：发 span -> 聚合落库 -> 查询 trace -> 查看状态页 -> 展示 benchmark 结果

## 备注
- 时间预算按 8 天冲刺控制，优先保答辩必演示链路，不追求页面全覆盖。
- 预计顺序：Trace 查询接口 -> TraceExplorer 联调 -> runtime stats -> Dashboard/ServiceMonitor -> Webhook 真实外发 -> Settings 收口 -> benchmark 材料。
- `docs/archive/current-task/MVP4.md` 已归档上一轮“Trace 主链路稳定化”工作，本文件从现在开始只描述当前轮次。
- Webhook 不是从零开始做：仓库里已有 `WebhookNotifier`、critical trace 触发点和 mock webhook；本轮重点是“真实平台适配 + 配置真生效 + 演示链路闭环”，先收口飞书，钉钉后置。
- `prompt_debug` 当前仅有表结构与缓冲写入能力占位，主链路没有稳定真实数据来源；本轮先不承诺该能力，避免前端继续保留假入口误导演示。
- Trace 耗时展示仍保留，因为它直接服务于调用链排障；但搜索条件先收口，不做 `min_duration/max_duration` 过滤，避免把“不完整上报导致的 0ms/失真耗时”误当成可靠检索条件。
- Trace 读侧已经完成最小手工联调：错误态、成功态、复杂拓扑态 3 组脚本都已准备，可直接往当前运行实例灌数验证列表、详情、AI 分析与瀑布图。
- 后续可能补一个 Trace 可视化小增强：当前瀑布图更偏“按 service 的时间泳道图”，适合看耗时与并行重叠，但不够直观看父子/子孙结构。若 Trace 主链与状态页验收通过、且仍有时间，优先考虑“小补而不是重写”：保留当前瀑布图作为时间视图，再补一个轻量 span tree / 缩进结构视图，专门展示 parent-child 层级；暂不计划重做整套瀑布图 y 轴。
