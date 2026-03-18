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
