# 20260318-fix-trace-analysis-batch

## Git Commit Message

fix(trace): 复用分析批量回写的风险更新语句

## Modification

- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_TraceReadSide.md

## Learning Tips

### Newbie Tips

- 事务能保证“中途失败就一起回滚”，但这不代表循环里每次都重新 `prepare` SQL 是合理的。数据一致性没问题，不等于执行路径就不浪费。
- SQLite 的 `prepare` 本质上要把 SQL 文本编译成可执行语句对象。既然 batch 里同一条 update 要跑很多次，就应该尽量复用同一个 stmt。

### Function Explanation

- `sqlite3_prepare_v2(...)`：把 SQL 编译成 `sqlite3_stmt*`。重复执行同一条 SQL 时，最值钱的是“prepare 一次，多次 reset/bind/step”。
- `sqlite3_reset(...)`：把 stmt 的执行游标重置回初始状态，准备下一次执行。
- `sqlite3_clear_bindings(...)`：清掉上一次绑定的参数，避免下一轮误用旧值。

### Pitfalls

- 如果 batch 里每条数据都重新 prepare 同一条 SQL，功能上虽然能跑，但会平白增加 SQL 编译开销。
- “批量处理”不等于一定要拼成一条超长 SQL。很多时候真正该做的是复用一条预编译语句，而不是硬凑复杂的 `CASE WHEN`。

## 追加记录（SearchTraces 可读性收口）

### Git Commit Message

refactor(trace): 简化 SearchTraces 参数绑定逻辑

### Modification

- server/persistence/SqliteTraceRepository.cpp

### 本次补充

- 移除了 `SearchTraces(...)` 里那套 `SqlParam + Type` 的参数容器写法。
- 改成函数内局部 binder lambda：
  - 专门负责把共享的 `WHERE` 条件参数按顺序绑定到 stmt
  - `COUNT(*)` 和 `SELECT ... LIMIT/OFFSET` 共用这一段绑定逻辑

### Learning Tips

#### Newbie Tips

- 动态 SQL 不一定非要引入一层“参数对象”才能复用逻辑。很多时候局部 lambda 更直接，因为你真正要复用的只是“绑定顺序”，不是一套通用类型系统。
- 看到 `SqlParam` 这种结构时，要先问一句：它是在解决真实复杂度，还是只是把简单事情包复杂了。

#### Pitfalls

- 如果为了复用两次绑定逻辑，就额外引入一套轻量运行时类型分发，代码可能会比重复两三行还更绕。

## 追加记录（SearchTraces 语义收口）

### Git Commit Message

refactor(trace): 去掉隐式 24h 筛选并收口索引定义

### Modification

- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_TraceReadSide.md

### 本次补充

- 去掉了 `/traces/search` 在 repo 层偷偷补“最近 24 小时”的默认时间窗。
- 现在只有前端明确传了 `start_time_ms/end_time_ms`，后端才会追加对应时间条件。
- 保留 `start_time_ms` 作为时间筛选字段，但不再在“未传时间”时强行加过滤。
- 移除了 `trace_summary(start_time_ms)`、`trace_summary(service_name)` 这两个单列索引定义，当前开发阶段只保留更贴近热查询路径的两个联合索引。

### Learning Tips

#### Newbie Tips

- “为了防全表扫而偷偷补默认条件”和“接口语义正确”是两回事。调用方没传时间筛选时，后端不应该悄悄替用户裁剪数据范围。
- 联合索引已经覆盖查询模式时，继续保留相近的单列索引，很多时候只是增加写入维护成本。

#### Pitfalls

- 如果动态 SQL 是“条件存在才追加占位符”，那 binder 也必须完全按同样的条件去 bind；两边一旦不同步，就会出现位置错位或 `SQLITE_RANGE`。

## 追加记录（搜索时间窗注释澄清）

### Git Commit Message

docs(trace): 澄清搜索时间窗和表字段的区别

### Modification

- server/persistence/SqliteTraceRepository.cpp

### 本次补充

- 在 `SearchTraces(...)` 的时间过滤注释里明确写清：
  - 请求体里的 `start_time_ms/end_time_ms` 表示“搜索时间窗边界”
  - 当前 SQL 比较的是 `trace_summary.start_time_ms`
  - 不是拿请求里的 `end_time_ms` 去比较表里的 `trace_summary.end_time_ms`

### Pitfalls

- 请求字段名和表字段名长得像，不代表它们语义相同。这个地方如果不写清，后面非常容易把“搜索窗口上界”误看成“trace 结束时间过滤”。
