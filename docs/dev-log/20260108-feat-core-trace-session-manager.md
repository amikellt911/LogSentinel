# 20260108-feat-core-trace-session-manager

## Git Commit Message
- feat(core): 初始化 TraceSessionManager 聚合与序列化骨架
- feat(core): 增加 trace_end/token 上限触发占位
- test(core): 增加 TraceSessionManager 基础单测
- feat(persistence): 新增 TraceRepository 占位接口
- docs: 更新任务与已知问题记录入口

## Modification
- AGENTS.md
- CurrentTask.md
- FutureMap.md
- docs/dev-log/20260108-feat-core-trace-session-manager.md
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/core/TokenEstimator.h
- server/core/TokenEstimator.cpp
- server/tests/TraceSessionManager_test.cpp
- server/ai/TraceAiProvider.h
- server/ai/MockTraceAi.h
- server/ai/MockTraceAi.cpp
- server/persistence/TraceRepository.h
- server/persistence/SqliteTraceRepository.h
- server/persistence/SqliteTraceRepository.cpp
- server/CMakeLists.txt
- docs/todo-list/Todo_TraceSessionManager.md
- docs/todo-list/Todo_TraceRepository.md
- docs/KnownIssues.md

## Learning Tips
### Newbie Tips
- 先搭建聚合骨架与序列化入口，再逐步补齐策略与存储逻辑，能降低重构成本。

### Function Explanation
- TraceSessionManager 负责聚合、触发与序列化，TraceRepository 提供原始与分析结果写入的抽象接口。

### Pitfalls
- 过早引入复杂依赖或策略会拖慢主线闭环；先保留占位与边界约束更稳妥。

## Dispatch Assembly

### Git Commit Message
- feat(core): 补齐 TraceSessionManager 分发组装流程

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/tests/TraceSessionManager_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260108-feat-core-trace-session-manager.md

### Learning Tips
#### Newbie Tips
- 先写清楚 summary/spans/analysis 的最小字段，有助于后续对齐前端 TraceExplorer。

#### Function Explanation
- Dispatch 中构建 TraceSummary 与 TraceSpanRecord，并在 AI 分析后写入 TraceAnalysisRecord。

#### Pitfalls
- 若上层对象生命周期短于线程池任务，捕获的指针可能失效；需确保管理器与依赖生命周期更长。

## Dispatch Refactor

### Git Commit Message
- refactor(core): 拆分 TraceSessionManager 组装逻辑

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260108-feat-core-trace-session-manager.md

### Learning Tips
#### Newbie Tips
- 把组装逻辑拆成小函数更容易单测和定位问题，也便于后续替换字段来源。

#### Function Explanation
- 新增 BuildTraceSummary / BuildSpanRecords / BuildAnalysisRecord 三个辅助方法。

#### Pitfalls
- 如果 helper 方法过多但缺少清晰职责边界，后续维护会变复杂，需保持命名一致。

## Additional Updates

### Git Commit Message
- feat(persistence): 拆分 TraceRepository 存储接口

### Modification
- server/persistence/TraceRepository.h
- server/persistence/SqliteTraceRepository.h
- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_TraceRepository.md
- docs/dev-log/20260108-feat-core-trace-session-manager.md

### Learning Tips
#### Newbie Tips
- 将 summary、spans、analysis 拆分存储能更好地服务不同查询场景，减少一次性写入负担。

#### Function Explanation
- TraceRepository 接口拆分为 `SaveTraceSummary`、`SaveTraceSpans`、`SaveTraceAnalysis`。

#### Additional Note
- 新增 `PromptDebugRecord` 与 `SavePromptDebug` 占位接口，便于后续 Prompt Debugger 追溯。
- PromptDebug 元数据拆分为 model、duration_ms、total_tokens、timestamp，便于前端展示。

#### Adjustment
- TraceSummary 移除 raw_payload/tags，tags 归入 TraceAnalysisRecord，保持职责清晰。

#### Pitfalls
- 过度耦合单一 payload 会让后续查询和迁移变得困难，拆分结构更利于演进。
