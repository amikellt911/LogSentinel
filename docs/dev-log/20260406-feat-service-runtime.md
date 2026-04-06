# Git Commit Message

feat(service-monitor): 新增服务监控运行态累加器与快照接口骨架

# Modification

- `server/core/ServiceRuntimeAccumulator.h`
- `server/core/ServiceRuntimeAccumulator.cpp`
- `server/handlers/ServiceMonitorHandler.h`
- `server/handlers/ServiceMonitorHandler.cpp`
- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `server/tests/ServiceRuntimeAccumulator_test.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`

# 这次补了哪些注释

- 在 `server/core/ServiceRuntimeAccumulator.h` 里补了服务监控快照、observation 和累加器接口的职责说明，明确“第一刀先锁契约，时间窗后补”。
- 在 `server/core/ServiceRuntimeAccumulator.cpp` 里补了 `OnPrimaryCommitted`、`OnAnalysisReady`、`OnTick` 的状态流转注释，说明为什么统计和样本要分两条更新路径。
- 在 `server/handlers/ServiceMonitorHandler.h` 和 `server/handlers/ServiceMonitorHandler.cpp` 里补了“为什么可以同步读快照、不需要线程池”的注释。
- 在 `server/core/TraceSessionManager.cpp` 里补了 observation span 视图异步拷贝和成功后再记账的注释，避免后面再把悬空指针和重复记账问题踩一遍。
- 在 `server/src/main.cpp` 里补了服务监控快照定时发布的注释，说明这条定时器当前只负责发布快照，后面分钟桶窗口仍复用它。

# Learning Tips

## Newbie Tips

把“统计更新”和“AI 摘要回填”拆成两个 observation，是为了避免同一条 trace 在两个成熟时机里被重复记账。不要因为它们都属于同一条 trace，就强行塞进一个统一入口。

发布快照和累加统计不是一回事。前者服务 HTTP 读路径，后者服务后台写路径。先把两者分开，后面要加分钟桶、时间窗、过期退账时才不会改一路崩一路。

## Function Explanation

`std::lock_guard<std::mutex>`：进入作用域时加锁，离开作用域自动解锁。这里适合保护服务监控里这类复合结构，因为里面不只是整数，还有 `unordered_map`、`vector` 和字符串样本。

`std::remove_if + vector::erase`：这是 C++ 里常见的“擦除-删除”写法。这里用它先删掉同一 `trace_id` 的旧样本，再插入新样本，避免一条 trace 在同一服务下重复展示。

`NLOHMANN_DEFINE_TYPE_INTRUSIVE`：给结构体声明 JSON 序列化/反序列化规则。这里直接把服务监控快照结构和 JSON 输出绑定，能避免 handler 里手写一堆字段拼装代码。

## Pitfalls

把局部 `vector` 的地址直接捕获给线程池任务是典型悬空指针坑。当前函数返回后局部对象就没了，worker 线程再访问就是未定义行为。这次专门把 observation 用到的 span 视图额外拷成 `shared_ptr<vector<...>>` 再交给异步任务。

滑动窗口场景里不要急着上“长期在线优先队列 topk”。因为数据既会加也会减，旧节点很容易 stale。第一刀先用完整 map 聚合，再在发布快照时排序截取，逻辑更稳。

当前服务监控统计还是“进程期累计态”，还不是最终的分钟桶窗口实现。如果后面前端标题已经写死“最近 30 分钟”，就必须尽快把时间窗退账接上，否则展示语义会漂。
