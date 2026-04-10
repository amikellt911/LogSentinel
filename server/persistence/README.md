# Persistence Notes

旧的 `SqliteLogRepository` 已经随 `/logs` 批处理老链一起从活代码和构建目标中移除。

当前仍在主链使用的持久化组件只有两块：

1. `SqliteTraceRepository`
   负责 Trace 结果、Span 明细与 Trace 查询读侧。

2. `SqliteConfigRepository`
   负责 Settings 的配置持久化与启动期配置回填。

如果后面要看真实主链的数据流，直接顺着下面这条看：

`POST /logs/spans -> TraceSessionManager -> BufferedTraceRepository -> SqliteTraceRepository`

不要再把这个目录下的旧仓储名字当成当前实现依据。
