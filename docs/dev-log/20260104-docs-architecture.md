# 20260104-docs-architecture

## Git Commit Message
- docs(readme): 补充系统整体架构简述

## Modification
- README.md
- docs/dev-log/20260104-docs-architecture.md

## Learning Tips
### Newbie Tips
- 设计日志系统时要先明确分层职责，避免在 IO 线程里做重逻辑导致整体吞吐下降。

### Function Explanation
- 本次仅补充文档说明，未新增或变更具体函数与库调用。

### Pitfalls
- 如果把“聚合、序列化、AI 调用”放在接入层同步执行，容易出现尾延迟抖动；将重任务下沉到后台线程池可避免这一问题。
