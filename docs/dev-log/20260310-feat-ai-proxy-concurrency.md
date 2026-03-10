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

追加记录（三）
Git Commit Message
feat(persistence): 接入双缓冲后台最小消费闭环

- `BufferedTraceRepository` 的后台 flush 线程现在已经不再只是空壳。它会用 `wait_for` 休眠，醒来后分别从 `full_primary_buffers_` 和 `full_analysis_buffers_` 各拿一桶出来，在无锁状态下调用底层 `sink_->SavePrimaryBatch(...) / SaveAnalysisBatch(...)`，写完后再 `clear()` 并回收到 free 池。
- 这一刀故意把锁边界收得很短：锁内只做“从满桶队列拿桶”和“把空桶放回 free 池”，真正慢的 batch flush 都放在锁外。既然前台 append 是高频路径，这个边界必须守住，不然后台一写库就会把前台全部堵死。
- 当前后台线程还没有接“按时间把没满的 current 桶主动切出去”这条逻辑。也就是说，这一版已经形成“前台满水位切桶 -> 后台消费满桶”的最小闭环，但定时触发 flush 还没真正生效，SQLite 底层也还没 override 成“一批一事务”的高效实现。

追加记录（四）
Git Commit Message
feat(persistence): 调整批量写接口为按表平铺直写

- 这条线又收了一次数据形状。既然 `PrimaryBufferGroup` 和 `AnalysisBufferGroup` 内部本来就是 SoA 形状（`summaries/spans`、`analyses/prompt_debugs`），那么后台 flush 不该再把它们重新组回 `vector<TracePrimaryWrite>` 或 `vector<TraceAnalysisWrite>`。这一步已经把 `TraceRepository` 的 batch 接口改成直接吃按表拆开的两个 vector，后台线程也改成把桶里的平铺数据原样交给底层 sink。
- 也就是说，现在后台 flush 的方向已经和桶内部形状一致了：主数据直接走 `SavePrimaryBatch(summaries, spans)`，分析结果直接走 `SaveAnalysisBatch(analyses, prompt_debugs)`，不再在缓冲层做多余重组。

追加记录（五）
Git Commit Message
feat(persistence): 将 SQLite Trace 批量写入改为一批一事务

Modification
- server/persistence/SqliteTraceRepository.h
- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_SqliteDoubleBuffer.md
- docs/dev-log/20260310-feat-ai-proxy-concurrency.md

Learning Tips
Newbie Tips
- “有 batch 接口”和“真的批量事务写入”不是一回事。既然底层如果还是循环调用旧的单条事务接口，那只是接口名变了，事务边界并没有变小，SQLite 该反复 `BEGIN/COMMIT` 还是会反复做。
- SQLite 这种单文件串行写场景里，批量优化最值钱的点不是神秘参数，而是把很多条 insert 包进一次事务里，同时把 `prepare` 次数降下来。既然事务提交和语句准备本身都有固定成本，那么一批一事务才有意义。

Function Explanation
- `SqliteTraceRepository::SavePrimaryBatch`
  这次改成了“一批一次事务”。既然 flush 线程已经把很多条 trace 的 `summary/spans` 攒进一个桶里，那么底层就应该在同一个事务里先循环写 `trace_summary`，再循环写 `trace_span`，中间只做 `bind/step/reset`，最后统一 `COMMIT`。
- `SqliteTraceRepository::SaveAnalysisBatch`
  逻辑和主数据批次一致，只不过写的是 `trace_analysis` 和 `trace_prompt_debug`。既然分析结果本来就是 AI 返回后的附加数据，那么这一批也应该一次事务提交，不要再退化成很多条小事务。

Pitfalls
- 不要在 batch 接口里再套一层旧的 `SaveSingleTraceAtomic`。既然旧接口自己内部就会开事务，那外层批次循环它，只会变成“大循环包小事务”，达不到这次优化的目的。
- 事务里循环多表写入时，`prepare` 应该尽量一批一次，而不是一条数据一次。否则即使事务合并了，语句准备成本还是会反复打在热路径上。
- 这一步虽然把 SQLite 底层 batch 事务接上了，但还没解决“时间到了却没满的 current 桶怎么 flush”和“关闭时如何强制 flush 最后一批 current 桶”。所以当前闭环仍然是“满水位驱动”优先，不要误以为整套双缓冲已经完全收尾。

追加验证
- `cmake --build server/build -j2` 已通过，`LogSentinel`、持久化模块和相关测试目标均能重新链接。
- `./server/build/test_sqlite_trace_repo` 已通过，12/12 通过。

追加记录（六）
Git Commit Message
feat(persistence): 补齐双缓冲定时切桶与关闭时 drain 语义

Modification
- server/persistence/BufferedTraceRepository.h
- server/persistence/BufferedTraceRepository.cpp
- docs/todo-list/Todo_SqliteDoubleBuffer.md
- docs/dev-log/20260310-feat-ai-proxy-concurrency.md

Learning Tips
Newbie Tips
- 双缓冲如果只会“满水位切桶”，那小流量场景会很别扭。既然 current 桶可能一直装不满，但数据已经在内存里放了很久，那么后台线程就必须定时醒来看看，必要时主动把 current 封箱送去 flush。
- 析构时只 drain `full_*_buffers_` 还不够。既然最后半桶数据可能还留在 `current_*` 里，那么 stop 阶段必须把 current 也强制切出去，不然最后那一批永远没有机会落库。

Function Explanation
- `ShouldFlushPrimaryCurrentByTimeLocked / ShouldFlushAnalysisCurrentByTimeLocked`
  这两个判断只看“当前桶是不是非空、首条数据进桶时间是不是已经超过 interval”。既然后台线程的定时语义只是兜底，那它就不该和前台按数量切桶那套判断混在一起。
- `TakeOnePrimaryBufferForFlushLocked / TakeOneAnalysisBufferForFlushLocked`
  这一步把“先拿满桶；如果没有满桶，再按时间或 drain 需要把 current 切出去”收成了统一入口。这样前台和后台虽然都能触发切桶，但真正的切桶动作始终通过同一套锁内路径完成，不会一边偷 current、一边往旧桶里写。

Pitfalls
- 不要让后台线程在无锁状态下直接碰 `current_*`。既然前台 append 也会改 current，那么后台如果不通过同一把锁切桶，马上就会回到你前面最怕的那种生命周期竞态。
- 定时唤醒不是时间轮场景。既然这里只有一个 flush 工人，不是几千个对象各自超时，那么 `wait_for(min(primary_interval, analysis_interval))` 就够了，别为了“有个定时器”再把时间轮搬进来。
- 这一步补的是 flush 语义，不是最终正确性证明。编译通过只能说明路径接起来了，还不能替代“主数据/分析结果都真正落库，关闭时最后半桶不丢”的专门验证。

追加记录（七）
Git Commit Message
fix(persistence): 修正双缓冲 flush 线程的条件变量唤醒语义

Modification
- server/persistence/BufferedTraceRepository.cpp
- docs/dev-log/20260310-feat-ai-proxy-concurrency.md

Learning Tips
Newbie Tips
- `condition_variable::wait_for(lock, timeout, pred)` 不是“有人 notify 我就立刻往下跑”。既然你传了 predicate，它真正关心的是 predicate 有没有变成 true。前面如果只拿 `stopping_` 当 predicate，那前台写满桶时 `notify_one()` 也不会让 flush 线程立刻处理工作。

Function Explanation
- `flush_cv_.wait_for(lock, sleep_interval)`
  这里现在改成只负责“让线程睡一会儿，或者被前台提前叫醒”。既然醒来以后反正都要自己检查 `stopping_`、满桶队列和时间水位，那就不该再把 `stopping_` 写成唯一 predicate。

Pitfalls
- 不要把“stop 条件”和“有活可干”硬揉进一个错误的 predicate。既然这条线程既要响应 `notify_one()`，又要定时兜底，那最简单也最不容易写歪的做法就是：先醒，再统一检查。

追加验证
- `cmake --build server/build -j2` 已通过，`LogSentinel` 与相关测试目标重新链接成功。
