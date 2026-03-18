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

## 追加记录（GetTraceDetail）

### Git Commit Message

feat(trace): 实现 trace 详情查询与同快照读取

### Modification

- server/persistence/SqliteTraceRepository.cpp
- server/tests/SqliteTraceRepository_test.cpp
- docs/todo-list/Todo_TraceReadSide.md

### 本次补充

- 实现了 `SqliteTraceRepository::GetTraceDetail(...)`
  - 先查 `trace_summary LEFT JOIN trace_analysis`
  - 再查 `trace_span`
  - 组装为 `TraceDetailRecord`
- 详情查询显式使用读事务，保证 summary / analysis / spans 基于同一份读取快照。
- 新增 `trace_span(trace_id, start_time_ms, span_id)` 联合索引，服务详情页的调用链排序读取。
- `tags` 当前明确返回空数组，因为后端主链路还没有真实 tags 落库。
- 增加仓库层测试：
  - 命中详情时返回 summary / analysis / spans
  - trace 不存在时返回 `nullopt`

### Learning Tips

#### Newbie Tips

- 多条 `SELECT` 想读到同一时刻的数据库状态，靠的不是“运气刚好没写入”，而是显式把它们包进一个读事务里。
- `LEFT JOIN` 适合详情页这种“一条 summary 对一条 optional analysis”的场景；但 spans 是一对多，硬 join 会把 summary 列重复很多次，组装代码会变脏。

#### Function Explanation

- `BEGIN; ... COMMIT;`：这里不是为了加写锁，而是为了把详情查询里的多次读取固定到同一个 WAL 快照上。
- `COALESCE(a.risk_level, s.risk_level)`：优先拿 analysis 的风险等级；如果 analysis 还没落库，再回退到 summary 里的缓存值。

#### Pitfalls

- 如果 summary 和 spans 分两次查，却不放进同一个读事务，那么 flush 线程刚好插入新 span 时，详情页就可能出现“顶部 span_count 和下面真实 spans 条数对不上”的撕裂。
- `tags` 结构体里虽然已经有字段，但这不代表后端已经真的写了 tags。当前这里只能稳定返回空数组，不能假装有真实数据。

## 追加记录（TraceExplorer 界面收口）

### Git Commit Message

refactor(trace-explorer): 删除假入口并收口服务筛选方式

### Modification

- client/src/views/TraceExplorer.vue
- client/src/components/TraceSearchBar.vue
- client/src/components/TraceListTable.vue
- client/src/types/trace.ts
- client/src/i18n.ts
- docs/todo-list/Todo_TraceReadSide.md

### 本次补充

- 从 `TraceExplorer` 页面移除了 `prompt_debug` 入口和对应抽屉挂载，避免继续展示本轮不做的假功能。
- 从搜索栏移除了“按耗时过滤”输入框。
- 把 `service` 从写死下拉改成可输入文本框，当前语义明确为“入口服务精确匹配”。
- 删除了 Trace 详情类型里的 `prompt_debug` 字段，避免页面继续误以为详情接口会返回这块数据。

### Learning Tips

#### Newbie Tips

- UI 上挂着一个按钮，不等于这个能力就算“以后再接”。如果后端当前没有稳定真实数据来源，最稳的是先把入口拿掉，不要让演示页面保留假动作。
- “下拉框好看”不代表它适合当前阶段。既然后端还没有服务枚举接口，前端把服务名改成输入框，反而更接近真实查询语义。

#### Pitfalls

- 如果前端保留 `prompt_debug` 按钮，但点击后只能拿 mock 或空数据，那么到联调和答辩时，这种假入口最容易露馅。
- 既然后端当前是 `service_name = ?` 精确匹配，那么前端就不能用模糊搜索的交互暗示用户，否则 UI 语义和后端实际行为会打架。

## 追加记录（TraceExplorer 列表联调）

### Git Commit Message

feat(trace-explorer): 接通 trace 列表真实查询

### Modification

- client/src/views/TraceExplorer.vue
- client/src/components/TraceListTable.vue
- client/src/types/trace.ts
- docs/todo-list/Todo_TraceReadSide.md

### 本次补充

- `TraceExplorer.vue` 不再用 mock 列表，而是实际调用 `POST /api/traces/search`。
- 搜索、翻页、改 pageSize 现在共用同一份查询状态，不再各自拼一套请求。
- 页面层增加了请求体转换：
  - `time_range/custom_time_*` -> `start_time_ms/end_time_ms`
  - `risk_level` -> 后端 `risk_levels`
  - `trace_id` 有值时按精确查询优先
- 页面层增加了响应映射：
  - `start_time_ms` -> 表格显示时间
  - `duration_ms` -> 表格耗时
  - 后端风险等级字符串 -> 前端徽章显示值
- `TraceListTable` 的分页状态改为由父页面控制，避免翻页后丢筛选条件。

### Learning Tips

#### Newbie Tips

- 只要分页是服务端分页，页码就属于“查询条件的一部分”，不能让表格组件自己偷偷维护一套本地页码。
- 后端 DTO 和前端表格行数据不是一回事。中间做一次映射，后面字段调整时才不会把每个组件都拖下水。

#### Pitfalls

- 如果搜索按钮和分页按钮各自拼请求体，最常见的问题就是“第一页搜得对，翻页条件全丢了”。
- 风险等级如果前端展示值和后端存储值大小写不一致，搜索条件就会出现“看着一样、实际上查不到”的假象。

### 验证说明

- 已运行 `cd client && npm run build`
- 构建仍被仓库里已有的前端 TypeScript 老问题阻塞，本次改动涉及的 `TraceExplorer/TraceListTable/types` 没有新增报错

## 追加记录（TraceExplorer 详情联调）

### Git Commit Message

feat(trace-explorer): 接通 trace 详情查询并映射调用链数据

### Modification

- client/src/views/TraceExplorer.vue
- client/src/types/trace.ts
- docs/todo-list/Todo_TraceReadSide.md

### 本次补充

- `TraceExplorer.vue` 点击列表行、AI 分析按钮、调用链按钮后，不再生成 mock 详情，而是实际调用 `GET /api/traces/{trace_id}`。
- 页面层新增了详情 DTO -> 前端展示对象的转换，继续兼容当前详情组件已有字段：
  - `analysis` -> `ai_analysis`
  - `start_time_ms/end_time_ms` -> 格式化时间字符串
  - `span.start_time_ms` -> 相对 trace 起点的 `start_time`
  - `span.duration_ms` -> `duration`
  - `span.raw_status` -> 前端展示状态 `success/error/warning`
- 新增详情抽屉加载态，避免请求过程里抽屉直接空白。
- 增加了简单的详情请求顺序保护，避免快速连续点击不同 trace 时旧响应覆盖新响应。

### Learning Tips

#### Newbie Tips

- 详情页最怕的不是“字段多”，而是后端 DTO 和前端旧组件字段不一致。这个时候最稳的是在页面层做一次转换，而不是把子组件一口气全改掉。
- 瀑布图横轴要的是相对时间，不是 Unix 毫秒绝对值。后端给绝对时间很正常，前端展示前自己减去 trace 起点就行。

#### Pitfalls

- 如果直接把后端 `raw_status` 喂给旧组件，颜色和文案会全乱，因为旧组件只认识 `success/error/warning`。
- 如果多个详情请求并发返回，而你不做最小的请求顺序保护，就会出现“最后点的是 B，结果页面显示成 A”的老问题。

### 验证说明

- 已运行 `cd client && npm run build`
- 构建仍被仓库里已有的前端 TypeScript 老问题阻塞，本次改动涉及的 `TraceExplorer/types` 没有新增报错
