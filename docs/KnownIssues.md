# KnownIssues

## KnownIssues
- （占位）

## TraceSessionManager
- Span 缺少 `end_time` 时的语义不明确（可能是上报缺失、长尾未结束或中途丢失）
- 同一 Trace 出现重复 `span_id` 的处理策略未定（覆盖、忽略还是直接结束）
- 当前重复 `span_id` 直接标记异常并提前分发，后续可能增加更细的策略或配置项
- Trace 长度异常膨胀的保护策略待定（单 Trace 上限、全局上限与降级策略）
- TraceSessionManager 的容量配置后续可能需要从 ConfigRepo 快照读取，具体缓存方式待定
- Root span 未到达或乱序导致的聚合不完整处理方式待定
- 扩展字段目前先用 `string` 承载，后续可升级为 `std::variant` 以提高类型安全
- TraceIndex/DFS 当前存在额外构建开销，后续可考虑减少查表与内存分配（children 存指针、复用容器、仅在必要时构建）
