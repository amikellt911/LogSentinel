Git Commit Message:
refactor(core): 为trace分发线程改造补充prepared缓存与inflight占位

Modification:
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:

Newbie Tips:
- 当一条 Trace 在“已离开 manager，但尚未进入 tombstone”的窗口里存在时，最容易出现晚到 span 误把旧 trace 复活的问题。哪怕第一步还没真正接通 dispatch 线程，也应该先把 inflight 占位结构钉住，不然后面状态机会越改越乱。
- `primary_enqueued` 只能表示“主数据已经成功送入 BufferedTraceRepository”，不等于“后续所有准备工作都完成了”。所以 prepared 缓存和 primary 阶段位要分开看。

Function Explanation:
- `std::optional<T>`：这里用来表达“prepared 数据可能还没有准备好”，比额外的 bool 标志更直接，也更不容易出现“标志位和真实对象不同步”的问题。

Pitfalls:
- inflight 表不要再次持有 session 所有权。它的职责只是做轻量拦截，如果又把完整 session 塞进去，就会和 dispatch job 形成双持有，后面回滚和释放顺序都会乱掉。

---

追加记录（2026-04-01，dispatch queue 骨架）：

Git Commit Message:
refactor(core): 增加有界dispatch队列与线程生命周期骨架

Modification:
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260401-refactor-trace-dispatch.md

Learning Tips:

Newbie Tips:
- 在独立线程真正接管业务前，可以先把“队列 + 线程生命周期 + 停机回收”骨架搭起来。这样后面逐步接业务逻辑时，风险会比一口气全改小很多。
- dispatch queue 的容量先和 worker queue 关联，是为了让上游比下游更早看到压力，而不是等 AI worker 真正堆满后才发现系统已经过载。

Function Explanation:
- `std::condition_variable::wait(lock, predicate)`：这里用谓词版本是为了同时处理“新任务到来”和“停止线程”两种唤醒原因，避免被假唤醒后写出空转逻辑。
- `std::thread::joinable()`：析构前先判断线程是否仍然可 join，避免对已经回收或未启动的线程再次 join。

Pitfalls:
- 线程析构顺序一定要先停 dispatch thread，再打印运行统计或销毁其他依赖；否则后面一旦真的把 dispatch 业务接进去，就会出现“后台线程还在跑、对象先析构”的悬空访问风险。

---

追加记录（2026-04-01，prepared 读取顺序收口）：

Git Commit Message:
refactor(core): 收口dispatch阶段的prepared缓存读取顺序

Modification:
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:

Newbie Tips:
- retry 路径里最容易写乱的一点，就是“session 自己已经带着 prepared 缓存回来了”，这时候应该先把缓存读出来，再判断还缺什么，而不是一上来就默认整条 trace 重算。
- `primary_enqueued` 和 `prepared_summary` 不是一回事。前者表示主数据是否已经 append 成功，后者表示 summary 是否已经算好并能复用；两者不能互相替代。

Function Explanation:
- `std::optional::has_value()`：这里用来区分“缓存是否存在”和“缓存对象里的字段值是什么”。既然 summary/payload 本身可能有合法默认值，就不该靠空字符串或零值去猜缓存状态。

Pitfalls:
- 当前 `order` 仍然依赖 `SerializeTrace(index, &order)` 这个副作用产出，所以“payload 已缓存但 primary 还没完成”时，仍可能为了拿 DFS 顺序做一次临时序列化。这个耦合这轮先记住，不要误以为 prepared 前置后就已经彻底消掉了全部重复 CPU 开销。
