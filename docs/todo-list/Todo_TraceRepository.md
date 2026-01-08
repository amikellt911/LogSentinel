# Todo_TraceRepository

- [x] 明确 TraceRepository 的职责与最小接口
- [x] 新建 TraceRepository 抽象接口
- [x] 新建 SqliteTraceRepository 占位实现
- [x] 将新实现加入 persistence_module 构建列表
- [x] 复核头文件路径与命名规范
- [x] 拆分 TraceRepository 接口为 summary/spans/analysis
- [x] 增加 PromptDebug 存储接口占位
- [x] PromptDebug 元数据拆分为独立字段
- [x] 移除 summary 的 raw_payload 与 tags，转移到 analysis
