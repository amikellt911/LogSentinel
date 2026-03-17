# Todo_TraceReadSide

- [x] 明确读侧最小范围：`/traces/search` + `/traces/{trace_id}`
- [x] 收口本轮不做项：`prompt_debug`、按耗时过滤
- [x] 明确列表 `service_name` 先按入口服务语义处理
- [x] 本步先补 `TraceQueryHandler` 骨架与请求参数解析，不扩到查询 SQL
- [x] 补 `SqliteTraceRepository` 的读侧查询方法签名
- [x] 接入 `query_tpool` 与 `trace_read_repo`
- [ ] 实现 `/traces/search` 列表查询
- [ ] 实现 `/traces/{trace_id}` 详情查询
- [ ] 补最小自测与联调
