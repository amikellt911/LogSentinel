# 20260109-feat-trace-tag-removal

## Git Commit Message
- feat(frontend): 移除 Trace 标签搜索
- feat(persistence): 同步移除 tags_json 字段

## Modification
- client/src/components/TraceSearchBar.vue
- client/src/types/trace.ts
- server/persistence/TraceRepository.h
- server/core/TraceSessionManager.cpp
- docs/TRACE_SCHEMA.md
- docs/dev-log/20260109-feat-trace-tag-removal.md

## Learning Tips
### Newbie Tips
- 功能裁剪时要同步前后端字段，避免接口与 UI 不一致。

### Function Explanation
- 删除 tags 搜索相关字段与表结构占位，减少写入与查询复杂度。

### Pitfalls
- 若只删前端不删后端，容易留下无效字段与误用路径。
