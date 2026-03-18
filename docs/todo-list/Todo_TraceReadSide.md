# Todo_TraceReadSide

- [x] 明确读侧最小范围：`/traces/search` + `/traces/{trace_id}`
- [x] 收口本轮不做项：`prompt_debug`、按耗时过滤
- [x] 明确列表 `service_name` 先按入口服务语义处理
- [x] 本步先补 `TraceQueryHandler` 骨架与请求参数解析，不扩到查询 SQL
- [x] 补 `SqliteTraceRepository` 的读侧查询方法签名
- [x] 接入 `query_tpool` 与 `trace_read_repo`
- [x] 实现 `/traces/search` 列表查询
- [x] 优化 `SaveAnalysisBatch` 的风险缓存回写：update stmt 改为预编译复用
- [x] 收口 `/traces/search` 语义：去掉隐式 24h 默认筛选，并移除冗余单列索引定义
- [x] 实现 `/traces/{trace_id}` 详情查询
- [x] 补最小仓库层自测：详情查询返回 summary / analysis / spans

## 下一阶段：TraceExplorer 前后端联调顺序

- [x] 冻结本轮 Trace 查询契约：确认前端只接 `/traces/search` 与 `/traces/{trace_id}`，不扩 `prompt_debug`
- [ ] 重写 `client/src/types/trace.ts`，先把前端类型对齐后端真实字段
- [ ] 收口 `TraceSearchBar`：去掉按耗时过滤，服务名从写死下拉改为可输入，时间范围统一换算为 `start_time_ms/end_time_ms`
- [x] 接通 `TraceExplorer.vue` 列表请求：真实调用 `/api/traces/search`，打通分页与筛选
- [x] 接通 `TraceExplorer.vue` 详情请求：真实调用 `/api/traces/{trace_id}`
- [x] 通过页面层 DTO 转换适配 `AIAnalysisDrawerContent` 与 `CallChainDrawerContent` 的现有字段
- [x] 在页面层完成 `TraceWaterfall` 所需的时间与状态映射：绝对时间转相对时间，`raw_status` 转前端展示状态
- [x] 去掉或隐藏 `PromptDebugger` 入口，避免保留假功能
- [x] 删除前端按耗时过滤 UI，并把 `service` 从写死下拉改成精确输入框
- [x] 提供手动灌 trace 的本地脚本，便于在当前运行实例上直接造联调数据
- [ ] 做一次最小联调验收：发 span -> 列表查询 -> 打开 AI 分析 -> 打开调用链

## 2026-03-19 计划

- [ ] 继续当前 Trace 读侧闭环，不开新主题
- [ ] 目标只做前端第一段：类型对齐 + 搜索栏收口 + 列表查询联调
- [ ] 如果列表链路当天打通，再决定是否继续做详情抽屉；否则详情顺延到下一步
