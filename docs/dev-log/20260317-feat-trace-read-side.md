# 20260317-feat-trace-read-side

## Git Commit Message

feat(trace): 增加 Trace 读侧 Handler 骨架与请求解析

## Modification

- server/handlers/TraceQueryHandler.h
- server/handlers/TraceQueryHandler.cpp
- server/persistence/SqliteTraceRepository.h
- server/persistence/SqliteTraceRepository.cpp
- server/src/main.cpp
- server/CMakeLists.txt
- docs/todo-list/Todo_TraceReadSide.md

## Learning Tips

### Newbie Tips

- 不要把“异步查询”理解成 SQLite 自己是非阻塞的。这里的异步只是指：阻塞查询不在 reactor 的 I/O 线程里跑，而是丢到工作线程执行，查完再切回 I/O 线程发响应。
- 读侧接口先把请求解析和执行通道骨架立住，再补 SQL，会比一开始把参数解析、线程模型、SQL 细节一起写更稳。否则后面一改执行模型，前面的查询代码大概率要返工。
- 分页参数一定要在入口就做收口。`page/page_size` 如果不在 Handler 里先限制边界，后面 repo 层、SQL 层、前端联调时都会重复兜底，代码会越来越乱。

### Function Explanation

- `ThreadPool::submit(...)`：把任务转交给工作线程执行。这里用于把 Trace 查询从 I/O 线程挪走，避免阻塞事件循环。
- `conn->getLoop()->queueInLoop(...)`：把发送响应这件事切回连接所属的 I/O 线程执行，保证 socket 发送仍然发生在正确的 EventLoop 线程里。
- `nlohmann::json::parse(...)`：把 HTTP body 解析成 JSON。这里先做 object 校验和字段类型校验，避免后面 repo 查询阶段再处理脏输入。

### Pitfalls

- 如果在 handler 里直接用 `req.path()` 截字符串取 `trace_id`，但不先去掉 query string，那么 `/traces/123?x=1` 这种请求会把 `?x=1` 一起当成 trace_id，后面查库一定错。
- `LEFT JOIN` 场景下，后续做风险筛选时如果直接按右表字段过滤，会把没有 analysis 的 trace 提前过滤掉。所以现在先把请求解析和字段语义定清，比急着写 SQL 更重要。
- 当前这一步只适合返回“not implemented yet”的占位响应，不能假装读侧已经打通。否则前端一接就会把协议和真实能力绑死，后面改起来更麻烦。

## 追加记录

### 本次补充

- 在 `SqliteTraceRepository` 里补上读侧两个同步方法签名：`SearchTraces(...)`、`GetTraceDetail(...)`，当前先返回未实现异常，占住 repo 入口。
- 在 `TraceQueryHandler` 里把 `submit(...)` 任务真正接到 repo 调用上：工作线程里执行查询，查完后切回连接所属 `EventLoop` 发 JSON。
- 在 `main.cpp` 接入独立 `query_tpool`、独立 `trace_read_repo`，并注册：
  - `POST /traces/search`
  - `GET /traces/*`
- 本次已经执行构建校验：`cmake --build build --target LogSentinel`

### 这一轮为什么先这么写

- 先把路由、Handler、线程池、Repo 调用链串通，等于先把“读请求从 HTTP 进来后，到底在哪个线程查库、怎么回包”这条时间线定死。
- 既然执行通道已经稳定，那么下一步只需要在 repo 里补 SQL，不需要再返工 main、Handler、线程切换逻辑。

### 新增坑点提醒

- 现在 `GET /traces/*` 用的是前缀路由，所以详情 handler 自己必须做 `trace_id` 提取和 query string 剥离；这件事不能指望 Router 帮你做。
- 查询线程池虽然独立了，但当前 repo 读方法还没加真实 SQL，所以前端现在拿到的是明确的 500 占位响应，不是最终行为。

## 追加记录（SearchTraces）

### Git Commit Message

feat(trace): 实现 trace 列表查询与风险等级同步缓存

### Modification

- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_TraceReadSide.md

### 本次补充

- 实现了 `SqliteTraceRepository::SearchTraces(...)`
  - `trace_id` 精确查询直接走主键
  - 普通分页查询走动态 `WHERE`
  - 默认时间窗兜底为最近 24 小时
  - 列表统一按 `start_time_ms DESC, trace_id DESC` 排序
  - 先 `COUNT(*)` 再 `LIMIT/OFFSET`
- 新增两个列表热路径联合索引：
  - `idx_trace_summary_start_time_trace_id`
  - `idx_trace_summary_service_start_time_trace_id`
- 在分析结果落库时，把 `trace_analysis.risk_level` 同步回写到 `trace_summary.risk_level`
  - 这样列表页热路径就能只查 `trace_summary`
  - 避免每次列表查询都 `LEFT JOIN trace_analysis`

### Learning Tips

#### Newbie Tips

- 联合索引不是“把所有列都塞进去”就一定更好。先服务 `WHERE + ORDER BY`，先解决扫描范围和排序，再看要不要为覆盖索引继续加返回列。
- `COUNT(*)` 不可怕，可怕的是在 `LEFT JOIN / 子查询 / COALESCE` 之后再数。既然列表页只查 `trace_summary`，那么 count 成本就可控很多。
- 时间范围筛选要统一语义。既然本轮列表是按 trace 开始时间搜，那就一直用 `start_time_ms`，不要一会儿拿开始时间，一会儿拿结束时间。

#### Function Explanation

- `sqlite3_prepare_v2(...)`：把 SQL 文本编译成可执行语句对象。动态 SQL 拼好以后，必须先 prepare，后面才能 bind 参数。
- `sqlite3_bind_int64 / sqlite3_bind_text`：把 C++ 侧参数安全绑定进 SQL，占位符顺序必须和拼接顺序一致。
- `sqlite3_step(...)`：驱动语句真正执行。查询时会不断返回 `SQLITE_ROW`，直到 `SQLITE_DONE`。

#### Pitfalls

- 不能写一条“大而全”的 SQL 再用 `(? IS NULL OR field = ?)` 这类写法图省事，这种写法很容易把索引计划搞废。
- 只按 `start_time_ms DESC` 排序不稳，因为同一毫秒可能有多条 trace。加上 `trace_id DESC` 才能让分页边界稳定。
- 如果 `trace_summary.risk_level` 不跟着 analysis 同步，列表页虽然查得快，但展示出来永远是最初的 `unknown`，这会直接把读侧语义搞假。
