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

