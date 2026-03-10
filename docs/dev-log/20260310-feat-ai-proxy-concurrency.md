# 20260310 feat ai-proxy-concurrency

Git Commit Message
feat(ai-proxy): 使用线程池桥接同步 provider 调用

Modification
- server/ai/proxy/main.py
- docs/todo-list/Todo_TraceAiProvider.md
- docs/dev-log/20260310-feat-ai-proxy-concurrency.md

Learning Tips
Newbie Tips
- `async def` 不是魔法。既然路由函数里还是直接调用同步阻塞函数，那么 event loop 一样会被卡死。外面写成 async，只代表“这里允许 await”，不代表内部同步代码会自动异步。
- 这次没有去改 `AIProvider` 基类和所有 provider 的函数签名，是因为当前真正的瓶颈在路由层。既然 `main.py` 已经是 async 路由，那么先把同步 provider 丢进线程池，就能先把 event loop 保护住；如果一上来把整套 provider 抽象都改成 async，会把 mock、gemini、基类接口一起卷进去，改动面会一下子膨胀。

Function Explanation
- `starlette.concurrency.run_in_threadpool`
  这是框架给 async 路由准备的桥接函数。既然当前 provider 还是同步 `def`，那就不能在 event loop 线程里直接跑。这里把同步函数丢到后台线程执行，外层继续 `await` 结果，event loop 自己就能去服务别的请求。
- `await request.body()`
  这是异步读取 HTTP body。既然 body 可能还在底层网络缓冲区里，那么读取动作本来就可能等待，所以这里应该是 await，而不是同步硬读。

Pitfalls
- 不要误以为“把同步函数名字改成 `async def`”就异步了。既然函数内部还是同步 SDK 调用、还是 `time.sleep`，那只是换皮，event loop 还是会被堵。
- 不要在 event loop 线程里直接跑第三方同步 SDK。既然像 Gemini 这种调用本质上还是同步网络请求，那么如果不做线程池桥接，单个慢请求就会把整个 Python proxy 的接入层一起拖慢。
- 线程池桥接解决的是“别堵住 event loop”，不是“全局并发策略已经设计好了”。如果后面要做模型降级、限流、优先级或者 fallback，这些还得在更外层单独控制，不能指望线程池本身替你兜底。

追加记录
- 新增 `server/tests/verify_ai_proxy_concurrency.py`，专门验证 `/analyze/trace/mock` 的固定并发总墙钟时间。
- 这次没有继续拿 `wrk` 做并发验证。既然 `wrk` 的模型是“连接一空就继续狂发”，那它更适合压吞吐，不适合验证 “5 个并发请求会不会被 `0.5s` 的 mock sleep 串成 2.5s” 这种问题。
- 验证脚本改用 `ThreadPoolExecutor + urllib.request` 做一轮固定并发。既然目标只是看 route 层有没有把阻塞 provider 从 event loop 挪走，那么这种“发一轮、量总耗时、看是否线性叠加”的方法更直接。

---

Git Commit Message
feat(persistence): 增加 Trace 双缓冲写入骨架

Modification
- server/persistence/TraceRepository.h
- server/persistence/BufferedTraceRepository.h
- server/persistence/BufferedTraceRepository.cpp
- server/CMakeLists.txt
- docs/todo-list/Todo_SqliteDoubleBuffer.md
- docs/dev-log/20260310-feat-ai-proxy-concurrency.md

Learning Tips
Newbie Tips
- 这次先落的是“壳子”，不是完整功能。既然并发缓冲写入很容易把生命周期、锁边界、SQL 批量逻辑揉成一团，那么第一刀先把类边界、成员状态、桶池初始化和后台线程生命周期立起来，后面再逐步接 append/cut-buffer/flush，排查起来会干净很多。
- `unique_ptr<BufferGroup>` 做桶的所有权转移时，用 `move`/`swap` 都是轻的。既然底层只是挪一个指针，不是在拷贝桶里两个大 `vector`，那热路径里真正该避免的不是“move 本身”，而是临时扩容和锁区过长。

Function Explanation
- `std::condition_variable::wait_for`
  这次 flush 线程没有上时间轮，而是用 `wait_for` 做“事件唤醒 + 定时兜底”。既然这里只有一个后台 flush 工人，不是几千个对象各自超时，那么 `wait_for` 更合适：前台写满时 `notify` 叫醒，前台没动静时线程自己按 interval 醒来检查。
- `reserve`
  这次桶初始化时就给 `summaries/spans/analyses/prompt_debugs` 预留容量，目的不是追求神秘优化，而是把热路径里频繁扩容摘掉。既然双缓冲的核心就是“桶复用”，那构造时 reserve，flush 后 clear 保留 capacity，才是这条线的基本盘。

Pitfalls
- 不要一上来把双缓冲逻辑揉进 `SqliteTraceRepository`。既然以后可能换别的底层 sink，那么缓冲层就应该是 `TraceRepository` 的包装器，而不是 SQLite 私货。SQLite 只负责“怎么批量写”，缓冲层负责“什么时候写、怎么换桶、后台线程怎么活”。
- 不要现在就把旧单条接口删掉。既然整个工程还在吃 `SaveSingleTraceAtomic` 这条老接口，那么先新增 batch 虚函数、把边界立起来更稳；后续再让 SQLite override 成“一批一次事务”。
- 这一步虽然已经把 `BufferedTraceRepository` 编进工程，但它现在仍然是透传到底层 sink 的骨架，还没有接真正的 append/cut-buffer/flush 行为。别把“编过了”误解成“双缓冲已经生效了”。

追加记录
- 为 `TraceRepository` 新增了 `TracePrimaryWrite / TraceAnalysisWrite` 以及 `SavePrimaryBatch / SaveAnalysisBatch` 两个批量接口，先把“缓冲层和底层 sink 的边界”钉死。
- 新增 `BufferedTraceRepository`，先立起两条缓冲线、两把锁、空桶池/满桶队列、单 flush 线程和 4 桶初始化（`current + next + free + free`）这套最小骨架。
- 当前 `BufferedTraceRepository` 还没有接真正的前台 append 和后台 flush，只是把生命周期和数据结构先落进代码里，并验证能完整编译通过。

追加修正
Git Commit Message
feat(persistence): 收窄双缓冲写入器接口边界

- 这条线中途收了一次接口形状。最开始把 `BufferedTraceRepository` 写成了“完整继承 `TraceRepository` 的装饰器”，但这和真实时间线不一致。既然当前持久化流程已经拆成“Dispatch 前写主数据”和“AI 返回后写分析结果”两段，那么缓冲层更自然的形状应该是：内部持有底层 `TraceRepository` sink，只对外暴露 `AppendPrimary / AppendAnalysis` 两个入口。
- 这次已经把类收窄到这两个 append 操作，不再伪装成完整 repository。这样后面继续接切桶和 flush 时，职责边界会更干净：底层 repository 负责“怎么把一批数据写进具体数据库”，缓冲层负责“什么时候切桶、什么时候 flush、后台线程怎么活”。

追加记录（二）
Git Commit Message
feat(persistence): 接入双缓冲前台最小切桶逻辑

- `BufferedTraceRepository` 现在已经接上了前台两条缓冲线的最小切桶逻辑：`AppendPrimary` 会把 `summary + spans` 写进 `current_primary_`，按 `spans.size()` 看主水位；`AppendAnalysis` 会把 `analysis + prompt_debug` 写进 `current_analysis_`，按 `analyses.size()` 看主水位。
- 两条线一旦达到各自主水位，就会执行同一套最小切桶动作：把 `current` move 进 `full` 队列、让 `next` 顶上来成为新的 `current`、再从 `free` 池补一只新的 `next`，最后 `notify_one()` 叫醒后台 flush 线程。
- 这一步还没有接“时间到切 current”和“后台线程真实 flush”。也就是说，现在前台切桶已经成形，但后台线程还只是生命周期骨架，SQLite 这边也还没有真正的一批一事务实现。
