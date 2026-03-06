# 20260109-feat-persistence-sqlite-trace-repo

## Git Commit Message
- feat(persistence): 初始化 SqliteTraceRepository 构造与建表

## Modification
- server/persistence/SqliteTraceRepository.h
- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_TraceRepository.md
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

## Learning Tips
### Newbie Tips
- 先在构造函数中完成建表与索引初始化，可以确保仓库在首次写入前处于可用状态。

### Function Explanation
- 构造函数负责打开数据库、启用外键/WAL，并创建 trace 相关表与索引。

### Pitfalls
- 若外键未启用，关联约束不会生效；若路径策略不统一，会导致数据落到不同目录。

## Additional Changes

### Git Commit Message
- test(persistence): 增加 SqliteTraceRepository 单测

### Modification
- server/tests/SqliteTraceRepository_test.cpp
- server/CMakeLists.txt
- docs/todo-list/Todo_TraceRepository_Test.md
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 用 :memory: 可以避免测试落盘，减少权限与清理成本。

#### Function Explanation
- 单测覆盖 SaveTraceBatch 的成功路径与可选分析/调试写入。

#### Pitfalls
- 如果 SaveTraceBatch 只返回 false 但不回滚，测试可能误判为成功。

### Additional Changes

#### Git Commit Message
- test(persistence): 增加落库校验查询

#### Modification
- server/tests/SqliteTraceRepository_test.cpp
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

#### Learning Tips
##### Newbie Tips
- 使用临时数据库文件可以在测试中验证真实写入结果。

#### Function Explanation
- 测试通过 SQLite 查询校验 summary/span/analysis/prompt_debug 的落库数量。

#### Pitfalls
- 忘记清理测试数据库会导致测试互相污染。

#### Adjustment
- 测试数据库使用显式相对路径，避免落到默认数据目录导致读取失败。

#### Additional Note
- 增加 summary 字段一致性校验，确认落库数据与输入一致。

## Additional Changes

### Git Commit Message
- refactor(persistence): SqliteTraceRepository 使用 SqliteHelper

### Modification
- server/persistence/SqliteTraceRepository.cpp
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 统一使用错误检查工具函数可以减少重复代码，保持错误处理一致。

#### Function Explanation
- 使用 `persistence::checkSqliteError` 处理建表失败场景。

#### Pitfalls
- 对非关键的 PRAGMA/索引设置不宜直接抛异常，避免初始化中断。

## Additional Changes

### Git Commit Message
- refactor(persistence): 抽出 TraceTypes 定义

### Modification
- server/persistence/TraceTypes.h
- server/persistence/TraceRepository.h
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 将 DTO 独立成类型文件可以减少头文件耦合，方便复用与扩展。

#### Function Explanation
- TraceSummary/TraceSpanRecord/TraceAnalysisRecord/PromptDebugRecord 移到 TraceTypes.h。

#### Pitfalls
- 若命名冲突或命名空间不清晰，可能导致类型歧义；需统一包含路径。

## Additional Changes

### Git Commit Message
- feat(persistence): 增加 Trace 批量保存接口占位

### Modification
- server/persistence/TraceRepository.h
- server/persistence/SqliteTraceRepository.h
- server/persistence/SqliteTraceRepository.cpp
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceRepository.md
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 先在内存中构建完整数据，再一次性落库能减少事务开销并提升写入速度。

#### Function Explanation
- 新增 `SaveTraceBatch` 接口占位并在 Dispatch 中调用。

#### Pitfalls
- 若批量保存失败未回滚，可能导致数据不一致；后续需补充事务处理。

## Additional Changes

### Git Commit Message
- feat(persistence): 实现 SaveTraceBatch 事务骨架

### Modification
- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_TraceRepository.md
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 批量写入使用事务能显著降低 I/O 次数并提升写入性能。

#### Function Explanation
- SaveTraceBatch 在单事务中完成 summary/spans/analysis/prompt_debug 写入。

#### Pitfalls
- 如果异常未触发回滚，可能导致数据半写入；捕获异常后必须显式 ROLLBACK。

## Additional Changes

### Git Commit Message
- feat(ai): 完善 MockTraceAi 的占位返回

### Modification
- server/ai/MockTraceAi.cpp
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 在无真实 AI 服务时提供固定返回，可用于验证主线闭环是否连通。

#### Function Explanation
- MockTraceAi 返回固定的 LogAnalysisResult 结构，避免空值导致后续流程异常。

#### Pitfalls
- 若占位返回字段缺失，可能导致落库或前端展示出错。
#### Adjustment
- 绑定使用 SQLITE_STATIC，前提是字符串来自入参对象且生命周期覆盖 sqlite3_step。


### Git Commit Message
- fix(core): 修复 TraceSession 构造参数不一致

### Modification
- server/core/TraceSessionManager.cpp
- server/tests/TraceSessionManager_test.cpp

### Learning Tips
#### Newbie Tips
- 保持构造函数签名与调用处一致，避免因接口漂移引发编译错误。

#### Function Explanation
- TraceSession 只保留 capacity 构造，Push 创建与测试用例同步更新。

#### Pitfalls
- 如果未来再次扩展构造参数，应同步更新测试与调用链。

### Git Commit Message
- docs(persistence): 补充 Trace SQLite 建表语句

### Modification
- docs/TRACE_SCHEMA.md

## Additional Changes

### Git Commit Message
- test(core): 增加 Trace 聚合闭环集成测试与代理入口

### Modification
- server/ai/proxy/main.py
- server/ai/proxy/providers/base.py
- server/ai/proxy/providers/mock.py
- server/ai/proxy/providers/gemini.py
- server/ai/MockTraceAi.cpp
- server/ai/MockTraceAi.h
- server/tests/TraceSessionManager_integration_test.cpp
- server/CMakeLists.txt
- docs/todo-list/Todo_TraceSessionManager_IntegrationTest.md

### Learning Tips
#### Newbie Tips
- 异步任务的落库测试要做等待轮询，否则容易在任务尚未执行时就判断失败。

#### Function Explanation
- Trace 聚合测试直接走 MockTraceAi 的 HTTP 调用链，验证 /analyze/trace/mock 能把分析结果写入 trace_analysis。

#### Pitfalls
- AI Proxy 未启动时分析会抛异常，集成测试会等待超时；需要先启动 proxy 或在测试前明确依赖。

## Additional Changes

### Git Commit Message
- test(core): 增加 Trace 落库字段一致性校验

### Modification
- server/tests/TraceSessionManager_integration_test.cpp
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 集成测试最好同时校验落库字段值，避免只验证数量导致逻辑偏差被掩盖。

#### Function Explanation
- 新增查询 summary/span/analysis 的字段校验，确保聚合与落库字段一致。

#### Pitfalls
- 如果测试只检查行数，可能漏掉字段映射错误或默认值错误。

## Additional Changes

### Git Commit Message
- feat(core): 序列化 Trace 时记录环异常信息

### Modification
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 遇到树形结构时，先做 visited 防环是最基础的安全措施，避免递归死循环。

#### Function Explanation
- SerializeTrace 在检测到环时记录 anomalies 字段，便于后续告警与 AI 解析。

#### Pitfalls
- 如果只跳过环节点而不记录异常，可能导致排查时无法定位输入异常。

## Additional Changes

### Git Commit Message
- test(core): 补充 Trace 集成测试边界场景

### Modification
- server/tests/TraceSessionManager_integration_test.cpp
- docs/dev-log/20260109-feat-persistence-sqlite-trace-repo.md

### Learning Tips
#### Newbie Tips
- 乱序与缺失父节点是常见边界输入，测试时要覆盖这些场景以避免上线后结构错误。

#### Function Explanation
- 增加环、缺失父节点、乱序 span 的测试用例，确保聚合与落库逻辑在异常输入下仍可稳定运行。

#### Pitfalls
- 若只测正常链路，异常场景可能导致树构建错误却难以及时发现。
