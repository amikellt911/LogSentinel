# CurrentTask

## 阶段信息
阶段：MVP5
开始日期：2026-03-17
归档日期：2026-04-10

## 当前阶段
MVP5：以前后端联调和答辩交付为目标，补齐 Trace 可查询、运行态可观测、Webhook 真实外发、配置项真实生效这四条最小闭环，并同步沉淀 benchmark 结果用于论文与答辩材料。

## 阶段目标
- [x] 打通 Trace 读侧闭环，让前端能真实查询 trace 列表、详情、调用链与 AI 分析
- [x] 把 Dashboard / ServiceMonitor 的核心展示区域接到真实后端运行时指标，而不是继续依赖 mock
- [x] 打通至少 1 个真实外部告警渠道，保证 critical trace 能真实外发（本轮先收口飞书 webhook）
- [x] 收口一批 Settings 关键字段，让主链配置不再停留在“能保存但完全不生效”的状态

## 本轮已完成
- [x] 后端补 Trace 查询接口：列表分页、按 trace_id 查详情、返回 spans / analysis
- [x] 前端 `TraceExplorer` 替换当前 mock 数据，接入真实 API
- [x] 暴露 Trace 主链路 runtime stats 接口，覆盖 `TraceSessionManager` 与 `BufferedTraceRepository` 关键指标
- [x] 前端 `Dashboard` / `ServiceMonitor` 至少接通一批核心真实指标
- [x] 收口 Webhook 告警链路：至少支持 1 个真实外部平台（先飞书），能把 critical trace 发到真实群机器人
- [x] 收口 Settings 主链关键字段：端口、线程数、Trace 聚合时序、水位阈值、Webhook、Prompt、AI provider/model/api_key、AI 总开关、熔断、自动降级、日志保留天数
- [x] 收口 Trace AI 可靠性骨架：`skipped_manual / skipped_circuit / failed_primary / failed_both / completed`
- [x] 清理旧 `/logs` 批处理链主路由和主构造链，当前活代码主线只保留 Trace 新链

## 验收结果
- [x] `TraceExplorer` 能真实看到 trace 列表与详情，而不是 mock 数据
- [x] `Dashboard` / `ServiceMonitor` 至少一批关键卡片来自真实后端指标
- [x] 至少有 1 次真实 Webhook 联调记录：critical trace 产生后，飞书群机器人能收到结构化告警消息
- [x] `TraceSessionManager` 单测、集成测已按当前 sealed/backpressure/AI 状态语义收口
- [x] 论文初稿所需的系统主框架、数据流、模块边界和特色定位已经基本稳定，可以开始撰写主体章节

## 备注
- MVP5 到这里收口，不再继续往这个阶段塞新目标。
- 后续新增需求统一挂到 `CurrentTask.md` 的 `v1.0.0` 版本目标里。
- `docs/archive/current-task/MVP4.md` 之前描述的是“Trace 主链路稳定化”；本文件归档的是“最小可演示闭环已经完成”的阶段状态。
