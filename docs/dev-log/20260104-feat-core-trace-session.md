# 20260104-feat-core-trace-session

## Git Commit Message
- feat(core): 新增 TraceSessionManager 占位类

## Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/CMakeLists.txt
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session.md

## Learning Tips
### Newbie Tips
- 先建立独立的占位类可以降低重构风险，让后续功能逐步迁移更可控。

### Function Explanation
- 本次仅新增类定义与构建项，无新增复杂函数或第三方库调用。

### Pitfalls
- 如果直接在原有类上叠加新逻辑，容易导致职责膨胀和回归风险；先拆出入口类可以避免失控扩展。
