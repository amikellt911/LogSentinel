# CurrentTask

## 阶段信息
阶段：MVP5
开始日期：2026-03-17
归档日期：

## 当前阶段
MVP5：以前后端联调和答辩交付为目标，补齐 Trace 可查询、运行态可观测、配置项真实生效这三条最小闭环，并同步沉淀 benchmark 结果用于论文与答辩材料。

## 阶段目标
- [ ] 打通 Trace 读侧闭环，让前端能真实查询 trace 列表、详情、调用链、AI 分析与 prompt 调试信息
- [ ] 把 Dashboard / ServiceMonitor 的核心展示区域接到真实后端运行时指标，而不是继续依赖 mock
- [ ] 收口 Settings 语义，确保页面上的配置项要么真实生效，要么明确标注“不在本轮生效”

## 本轮范围
要做：
- [ ] 后端补 Trace 查询接口：列表分页、按 trace_id 查详情、返回 spans / analysis / prompt_debug
- [ ] 前端 `TraceExplorer` 替换当前 mock 数据，接入真实 API
- [ ] 暴露 Trace 主链路 runtime stats 接口，覆盖 `TraceSessionManager` 与 `BufferedTraceRepository` 关键指标
- [ ] 前端 `Dashboard` / `ServiceMonitor` 至少接通一批核心真实指标
- [ ] 梳理 Settings 页面字段，去掉假重启、假生效或误导性语义
- [ ] 固定 benchmark 命令、参数、结果模板，沉淀论文可直接引用的数据和截图

不做：
- [ ] 不做新一轮高并发极限优化
- [ ] 不做完整监控系统或 Redis / 缓存扩展
- [ ] 不做 `Benchmark` 页面真联调，benchmark 结果以脚本和文档为主
- [ ] 不做 `History` / `LiveLogs` 老链路大修
- [ ] 不做跨进程去重、分布式配置热更新、主线程模型重构

## 核心任务
- [ ] 先补 `SqliteTraceRepository + Handler + Router` 的 Trace 读侧能力，形成最小可查询后端接口
- [ ] 再接通 `TraceExplorer`，把列表、详情抽屉、调用链、Prompt Debug 全部切到真实数据
- [ ] 再把 runtime stats 暴露给前端，收口系统状态与背压展示
- [ ] 最后清理 Settings 的假能力，并完成 benchmark 结果归档与联调验收

## 验收标准
- [ ] `TraceExplorer` 能真实看到 trace 列表与详情，而不是 mock 数据
- [ ] `Dashboard` / `ServiceMonitor` 至少一批关键卡片来自真实后端指标
- [ ] `Settings` 页面不再出现“能保存但当前主链路根本不吃”的假功能误导
- [ ] 有一套可复跑的 benchmark 命令、结果记录和截图，可直接用于论文与答辩
- [ ] 有一次完整联调记录：发 span -> 聚合落库 -> 查询 trace -> 查看状态页 -> 展示 benchmark 结果

## 备注
- 时间预算按 8 天冲刺控制，优先保答辩必演示链路，不追求页面全覆盖。
- 预计顺序：Trace 查询接口 -> TraceExplorer 联调 -> runtime stats -> Dashboard/ServiceMonitor -> Settings 收口 -> benchmark 材料。
- `docs/archive/current-task/MVP4.md` 已归档上一轮“Trace 主链路稳定化”工作，本文件从现在开始只描述当前轮次。
