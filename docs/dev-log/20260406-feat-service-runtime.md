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
- `client/src/views/ServiceMonitorPrototype.vue`

# 这次补了哪些注释

- 在 `server/core/ServiceRuntimeAccumulator.h` 里补了服务监控快照、observation 和累加器接口的职责说明，明确“第一刀先锁契约，时间窗后补”。
- 在 `server/core/ServiceRuntimeAccumulator.cpp` 里补了 `OnPrimaryCommitted`、`OnAnalysisReady`、`OnTick` 的状态流转注释，说明为什么统计和样本要分两条更新路径。
- 在 `server/handlers/ServiceMonitorHandler.h` 和 `server/handlers/ServiceMonitorHandler.cpp` 里补了“为什么可以同步读快照、不需要线程池”的注释。
- 在 `server/core/TraceSessionManager.cpp` 里补了 observation span 视图异步拷贝和成功后再记账的注释，避免后面再把悬空指针和重复记账问题踩一遍。
- 在 `server/src/main.cpp` 里补了服务监控快照定时发布的注释，说明这条定时器当前只负责发布快照，后面分钟桶窗口仍复用它。
- 在 `client/src/views/ServiceMonitorPrototype.vue` 里补了“为什么这一刀只接顶部 overview、下面继续保留 mock”的注释，避免后面联调时把整页状态一口气改乱。

# 追加记录：2026-04-06 前端 overview 真数据接线

## Git Commit Message

feat(frontend): 接入服务监控原型页顶部总览真数据

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `overviewCards` 附近补了注释，明确“这一刀只接顶部总览，服务卡和右侧详情继续保留 mock”，避免前后端联调范围失控。

## Learning Tips

### Newbie Tips

前后端联调不要一上来整页全接。先挑一块最独立、最容易验证的数据区，比如顶部总览。这样接口、时间格式、刷新按钮、兜底回退都会先暴露出来，调试面最小。

### Function Explanation

`onMounted`：组件挂载后执行。这里用它在页面第一次进入时主动拉一次 overview，避免用户还没点刷新就看到全是 mock 顶部数字。

`dayjs(...).format('HH:mm:ss')`：把后端毫秒时间戳转成前端原型页当前正在用的时分秒字符串，避免顶部总览和下面 mock 列表的时间展示风格不一致。

### Pitfalls

当前 `npm run build` 仍会被仓库里已有的 TypeScript 旧问题拦住，不能把这次 overview 接线误判成构建失败。为了确认当前改动没有把页面语法打坏，这次额外跑了 `npx vite build`，结果通过。

# 追加记录：2026-04-06 原型页全局异常操作排行真数据接线

## Git Commit Message

feat(frontend): 接入服务监控原型页全局异常操作排行真数据

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `operationRanking` 附近补了注释，说明右下角全局排行和顶部 overview 共用同一个后端快照接口，当前仍然只把这一块切成真数据。

## Learning Tips

### Newbie Tips

前端联调时，能复用同一个后端快照接口就别额外拆多个请求。这样刷新时机、错误处理和兜底逻辑都集中在一个地方，后面继续接服务卡时也更容易保持一致。

### Function Explanation

`computed`：依赖变了才重新算。这里让 `operationRanking` 在“有真数据时吃后端，没有真数据时退回 mock”之间自动切换，不需要额外手动同步两份展示状态。

### Pitfalls

后端这块 `global_operation_ranking[].count` 的语义是“异常 span 次数”，不是“异常链路数”。所以这次顺手把原型页右下角文案改成了“异常次数”，避免前后端口径打架。

# 追加记录：2026-04-06 原型页服务卡基础统计真数据接线

## Git Commit Message

feat(frontend): 接入服务监控原型页服务卡基础统计真数据

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `services` 计算属性附近补了注释，明确“左侧服务卡吃后端基础统计，右侧详情继续复用 mock”。
- 在 `fetchRuntimeSnapshot()` 里补了选中服务同步的注释，说明为什么切成后端 top4 后需要把失效的选中项挪到榜首服务。

## Learning Tips

### Newbie Tips

前端从 mock 过渡到真接口时，别直接把旧对象全删掉。更稳的做法是“把真数据覆盖基础统计字段，把复杂详情继续挂在 mock 上”，这样联调范围能被切得很小。

### Function Explanation

`computed<ServiceItem[]>`：这里把后端 `services_topk` 转成页面原来就在用的 `ServiceItem` 形状。这样模板层不用一起大改，变化被收口在脚本的数据适配层。

### Pitfalls

后端 `services_topk` 是动态 top4，当前选中的服务名可能突然不在榜里。如果不在拉取后同步 `selectedServiceName`，右侧面板就会继续悬着一个已经不在左侧榜单里的旧 mock 服务。

# 追加记录：2026-04-06 原型页最近异常 Trace 样本真数据接线

## Git Commit Message

feat(frontend): 接入服务监控原型页最近异常样本真数据

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `recentTraces` 数据适配处补了注释，说明后端已经按 `trace_id + service` 去重并裁成最近 3 条，前端这里只做字段名转换。

## Learning Tips

### Newbie Tips

后端已经决定好的排序和去重规则，前端就别再重新算一遍。否则两边一旦口径不一致，页面上同一块数据就会在刷新后跳来跳去，很难排查。

### Function Explanation

数据适配层的作用就是“把后端结构翻译成当前页面结构”。这次 `recent_samples[] -> recentTraces[]` 的转换就是典型例子：模板完全不用改，变化都被收口在计算属性里。

### Pitfalls

当前是“有后端 recent_samples 就优先吃后端，没有才退回 mock”。所以如果某个服务进入 top4 但后端样本暂时还没回来，页面仍可能先看到 mock 样本。这是当前刻意保留的过渡态，不是 bug。

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
