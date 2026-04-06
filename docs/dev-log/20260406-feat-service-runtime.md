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

# 追加记录：2026-04-06 原型页右侧详情剩余模块收口

## Git Commit Message

feat(frontend): 收口服务监控原型页右侧详情真数据适配

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `services` 数据适配层补了注释，说明服务摘要和右侧问题摘要当前先从 `recent_samples[].summary` 派生，不额外扩后端字段。

## Learning Tips

### Newbie Tips

后端字段不够时，先看能不能从现有真字段稳定派生，而不是立刻扩接口。只要派生规则清楚、前后端都能解释，这种收口比临时加新字段更稳。

### Function Explanation

`.filter((summary, index, arr) => arr.indexOf(summary) === index)`：这是最朴素的去重写法。这里样本上限本来就只有 3，所以不用为了这点数据再上 `Set` 或额外容器。

### Pitfalls

当前右侧“最近问题摘要”并不是真正的服务级 AI prompt 产物，而是从最近样本 summary 派生出来的过渡态。演示没问题，但如果后面论文或答辩要强调“按服务分析”，就还得单独补服务级摘要能力。

# 追加记录：2026-04-06 手工联调脚本与受限环境启动开关

## Git Commit Message

feat(test): 补充服务监控手工联调脚本与代理启动开关

## Modification

- `server/tests/manual_service_monitor_demo.sh`
- `server/src/main.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/tests/manual_service_monitor_demo.sh` 里补了整段中文注释，说明脚本只做三件事：灌复杂 trace、等待快照发布、拉 runtime JSON。
- 在 `server/src/main.cpp` 里给 `--no-auto-start-proxy` 补了注释，说明这个开关只用于受限环境下手动先起 proxy 的联调场景，不改变默认开发行为。

## Learning Tips

### Newbie Tips

联调脚本最值钱的不是“多自动化”，而是“别重复发明 payload”。既然仓库里已经有复杂 trace fixture，就应该直接复用它，避免后面一份脚本测的是 A 口径，另一份 fixture 测的是 B 口径。

### Function Explanation

`trap 'kill ...' EXIT`：shell 退出时自动清理后台进程。这里本来是为了把 proxy 和后端放在一个 shell 会话里收尾，不过最终真实 smoke 走的是“手动复用现成 proxy + 后端禁自动起 proxy”的路径。

### Pitfalls

这次联调顺手暴露了一个环境相关问题：在沙箱里直接跑 C++ 后端时，监听 socket 的系统调用会被拦成 `EPERM`。这不是业务逻辑 bug，所以最后那轮真实 smoke 是放到沙箱外完成的。

## Smoke Result

在沙箱外完成了一轮真实联调：

- 后端启动命令：
  - `./LogSentinel --no-auto-start-proxy --trace-ai-provider mock --trace-ai-base-url http://127.0.0.1:8001 --db /tmp/logsentinel_service_monitor_demo.db --port 18080`
- 灌数脚本：
  - `bash server/tests/manual_service_monitor_demo.sh http://127.0.0.1:18080`
- 结果摘要：
  - `overview.abnormal_service_count = 3`
  - `overview.abnormal_trace_count = 1`
  - `services_topk` 返回 `order-service / payment-service / bank-gateway`
  - 每个服务都带回了 `operation_ranking` 和 `recent_samples`
  - `global_operation_ranking` 返回 3 条异常操作

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

# 追加记录：2026-04-06 联合启动联调脚本

## Git Commit Message

feat(test): 新增服务监控联合启动联调脚本

## Modification

- `server/tests/run_all_and_demo.sh`

# 追加记录：2026-04-06 原型页自动刷新改为 3 秒

## Git Commit Message

fix(frontend): 将服务监控原型页自动刷新调整为3秒

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `autoRefreshIntervalMs` 附近补了中文注释，说明为什么前端轮询频率要和后端 `3s` 桶粒度对齐。
- 在 `onMounted()` 的自动轮询注释里补了“后端 1 秒发布 + 3 秒桶 + 前端 3 秒拉取”的关系说明，避免后面再把轮询改回明显更钝的节奏。

## Learning Tips

### Newbie Tips

前端自动刷新不是越慢越省事。既然服务监控页面的意义是看“最近状态”，那么它的轮询频率至少要和后端桶封口粒度一个量级，不然用户看到的就不是“统计慢”，而是“页面像没请求”。

### Function Explanation

`setInterval(..., 3000)` 在这里不是为了做实时系统，而是为了让页面拉取节奏和后端 `3s bucket` 基本对齐。这样一条异常 trace 进窗后，下一轮前端轮询通常就能看见。

### Pitfalls

如果前端轮询远慢于后端进窗节奏，就会出现一种很迷惑的假象：后端 `/service-monitor/runtime` 已经有数据了，但页面还是空的。用户会误以为“前端根本没发请求”，其实只是轮询太钝。

# 追加记录：2026-04-06 服务监控正式入口切换到原型页

## Git Commit Message

refactor(frontend): 将服务监控正式入口切换到原型页

## Modification

- `client/src/router/index.ts`
- `client/src/layout/MainLayout.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/router/index.ts` 里补了中文注释，说明为什么 `/service` 现在直接指向 `ServiceMonitorPrototype.vue`，以及为什么保留 `/service-prototype -> /service` 的兼容重定向。

## Learning Tips

### Newbie Tips

页面替换时，优先切正式入口，不要让“老入口 + 新入口”长期并存。否则用户、联调脚本和你自己都会搞混，到底哪个页面才是当前真版本。

### Function Explanation

`redirect: '/service'`：这里不是再保留一套页面，而是把旧地址统一跳到新正式入口。这样旧书签还能用，但系统里只有一个真实页面在工作。

### Pitfalls

如果只改路由，不改左侧菜单，用户仍然会以为系统里有两个“服务监控页面”。这次把导航入口一起收掉，就是为了避免这种语义脏掉的状态。

# 追加记录：2026-04-06 服务监控 30 分钟单窗口时间窗

## Git Commit Message

feat(service-monitor): 接入服务监控30分钟单窗口时间窗

## Modification

- `server/core/ServiceRuntimeAccumulator.h`
- `server/core/ServiceRuntimeAccumulator.cpp`
- `server/tests/ServiceRuntimeAccumulator_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/core/ServiceRuntimeAccumulator.h` 里补了分钟桶、窗口累计态、最近态三者的职责注释，明确哪些字段进窗、哪些字段不进窗。
- 在 `server/core/ServiceRuntimeAccumulator.cpp` 里补了 `OnPrimaryCommitted()`、`OnTick()`、`EnsureBucketForMinuteLocked()` 的注释，说明为什么 observation 先写活跃分钟桶、为什么只能把已封口分钟推进进窗口、为什么桶数量要比窗口分钟数多一个。
- 在 `server/tests/ServiceRuntimeAccumulator_test.cpp` 里补了两条新测试的中文注释，分别锁定“同一分钟不提前发布”和“过期分钟会退窗”。

## Learning Tips

### Newbie Tips

滑动窗口别直接在高频写路径上改“最终总数”。更稳的方式是先把这一分钟发生的增量记到分钟桶里，等分钟结束后再统一把这个桶并进窗口累计态。这样后面要做退窗时，才有东西可以减回去。

“最近态”和“窗口累计态”不是一回事。像 `recent_samples`、`latest_exception_time_ms` 这种表达“最近一次/最近三条”的字段，强行塞进分钟桶反而会把语义搞乱。

### Function Explanation

`std::function<int64_t()>`：这里被用来注入“当前单调毫秒时间”的获取方式。生产环境走默认 `steady_clock`，测试里则可以喂一个手动推进的假时间，这样时间窗测试就不用真的等一分钟。

`std::chrono::steady_clock`：单调时钟，只表示“经过了多久”，不会因为系统校时往回跳。做时间窗推进时，它比 `system_clock` 更适合。

### Pitfalls

如果桶数量和窗口分钟数完全相同，那么“当前活跃分钟”和“窗口里最老的那个分钟”可能会撞到同一个槽。这里专门把桶数设成 `window_minutes + 1`，就是为了给活跃分钟留一个独立槽位。

时间窗如果只做“进窗”不做“退窗”，页面看起来会先正常，跑久了就会重新退化成“进程累计态”。这次把退窗一起接进来了，不然后面联调再看榜单会越来越脏。

## 追加说明：函数级注释补齐

- 继续在 `server/core/ServiceRuntimeAccumulator.h` 里补了每个公开接口和每个内部辅助函数的中文注释，重点解释“为什么 observation 先写桶、为什么只推进已封口分钟、为什么 recent/latest 不进窗”。
- 继续在 `server/core/ServiceRuntimeAccumulator.cpp` 里补了核心函数的中文注释，尤其是 `OnPrimaryCommitted()`、`OnTick()`、`EnsureBucketForMinuteLocked()`、`ApplyBucketToWindowLocked()` 这些最容易把时间窗语义看混的地方。

# 追加记录：2026-04-06 服务监控联调窗口启动参数

## Git Commit Message

feat(main): 增加服务监控窗口分钟数启动参数

## Modification

- `server/src/main.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/src/main.cpp` 的参数解析处补了 `--service-monitor-window-minutes` 的中文注释，说明这个参数主要用于联调时把窗口临时压到 `1~2` 分钟，方便观察进窗和退窗。

## Learning Tips

### Newbie Tips

产品默认值和联调时间尺度不一定要绑死。窗口对外语义可以继续默认 30 分钟，但启动时留一个小参数口子，联调和回归就不用真等半小时。

### Function Explanation

`std::stoi`：把命令行传进来的字符串解析成整数。这里配合参数校验，把 `--service-monitor-window-minutes` 直接转成 `int`，再传给运行态累加器。

### Pitfalls

这种联调参数最好只影响“运行时实例化配置”，不要反过来去改前端文案或协议字段。否则你只是为了方便测试，却会把展示语义一起搞乱。

# 追加记录：2026-04-06 联合联调脚本默认透传小窗口

## Git Commit Message

feat(test): 联合联调脚本默认透传服务监控小窗口参数

## Modification

- `server/tests/run_all_and_demo.sh`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/tests/run_all_and_demo.sh` 里补了 `SERVICE_MONITOR_WINDOW_MINUTES` 的中文注释，说明为什么联合脚本默认压到 2 分钟，以及如何通过环境变量覆盖。
- 在同一个脚本里补了后端启动命令前的中文注释，说明把 `--service-monitor-window-minutes` 收进脚本是为了省掉重复手敲启动参数。

## Learning Tips

### Newbie Tips

联合脚本的价值就在于把“每次都一样的参数”收进去。既然联调退窗时几乎每次都要把窗口压小，那就不该让人反复手敲同一个启动参数。

### Function Explanation

`VAR="${VAR:-default}"`：这是 shell 里很常见的“环境变量优先，否则给默认值”的写法。这里让联合脚本默认用 2 分钟窗口，但你需要时仍然可以外部覆盖。

### Pitfalls

shell 的续行命令里不要把注释放在反斜杠链中间，不然很容易把整条命令解析坏。注释最好写在命令块上方，而不是塞进 `\` 续行里。

# 追加记录：2026-04-06 去除服务监控原型页运行态 mock fallback

## Git Commit Message

fix(frontend): 去除服务监控原型页运行态mock回退

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `services` 计算属性附近补了中文注释，明确这刀的目标是“后端真数据或空态”，不再偷偷回退到 mock。
- 在 `fetchRuntimeSnapshot()` 里补了中文注释，说明当时间窗退空时为什么要把 `selectedServiceName` 一起清掉。
- 在模板的空态块附近补了中文文案，明确当前服务榜、样本列表、问题摘要、操作表、全局排行在无数据时都应该显示空态。

## Learning Tips

### Newbie Tips

联调时间窗时，页面绝对不能保留“没数据就回退 mock”的逻辑。否则后端已经退窗了，前端却还在显示假卡片，你根本分不清是时间窗没生效，还是页面在兜底骗人。

### Function Explanation

`computed(...)`：这里继续让页面展示完全由响应式数据驱动，但数据源改成了“只吃 runtime 快照”。没有快照数据时，直接回落到空数组和空态对象，而不是旧 mock。

### Pitfalls

右侧聚焦面板依赖 `selectedService`。如果服务榜退空了却不把当前选中值一起清掉，右侧就会继续挂着上一轮的旧服务，视觉上等价于“假数据没退掉”。

# 追加记录：2026-04-06 服务监控原型页自动刷新

## Git Commit Message

feat(frontend): 为服务监控原型页增加自动刷新

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `onMounted()` 附近补了中文注释，说明为什么把自动刷新定成 10 秒，以及它和后端 5 秒快照发布节奏的关系。
- 在同一文件的 `onBeforeUnmount()` 附近补了中文注释，说明页面卸载时为什么必须清理定时器。

## Learning Tips

### Newbie Tips

前端自动刷新别只看“想不想方便”，还要看后端发布节奏。后端现在 5 秒发一次快照，前端拉得比这个还快意义不大；10 秒刚好能看到变化，又不会无意义地频繁打接口。

### Function Explanation

`window.setInterval(...)`：浏览器定时器。这里定期触发 `fetchRuntimeSnapshot()`，让服务监控页在不手点刷新的情况下也能看到进窗和退窗。

`onBeforeUnmount(...)`：组件销毁前的清理钩子。这里专门用来 `clearInterval`，避免页面切走后还继续后台轮询。

### Pitfalls

自动刷新和手动刷新共用同一个请求函数时，必须有并发保护。当前继续复用 `overviewLoading` 这个短路判断，避免网络慢时定时器又叠一层请求上去。

# 追加记录：2026-04-06 服务监控联调脚本职责拆分

## Git Commit Message

refactor(test): 拆分服务监控联调起服务与发数脚本

## Modification

- `server/tests/manual_service_monitor_demo.sh`
- `server/tests/post_random_trace_once.py`
- `server/tests/run_all_and_demo.sh`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/tests/manual_service_monitor_demo.sh` 里补了整段中文注释，明确它现在只负责起服务、写 `run_info.env`、以及在默认模式下驻留。
- 在 `server/tests/post_random_trace_once.py` 里补了中文注释，说明它只负责发送 1 条随机 `trace_key` 的复杂 trace，不参与起服务和拉 JSON。
- 在 `server/tests/run_all_and_demo.sh` 里补了中文注释，说明它现在只是包装器：先起服务，再发一条随机 trace，再拉一次 runtime JSON。

## Learning Tips

### Newbie Tips

联调脚本最忌讳“一个脚本把起服务、发数、校验、驻留全糊在一起”。一旦其中一段想复用或替换，整坨都得一起改。拆成“起服务脚本 + 发数脚本 + 包装器”之后，单独调试哪一段都更直接。

### Function Explanation

`source run_info.env`：把起服务脚本写出来的运行信息重新读进当前 shell。这样包装器就能拿到 PID、日志路径、URL，而不用再从日志里硬解析。

`random.Random(seed_ms)`：先用时间毫秒做种子，再生成随机 `trace_key`。这样每次运行默认不一样，但需要复现时仍然可以把 `seed_ms` 打印出来。

### Pitfalls

detach 模式下最容易踩的坑就是“子脚本退出时把刚起好的进程一起清掉”。这次在起服务脚本里专门加了 `KEEP_CHILDREN_ON_EXIT`，只有把所有权交给外层包装器之后，EXIT trap 才不再回收子进程。
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/tests/run_all_and_demo.sh` 顶部补了整段流程注释，明确脚本负责“编译后端、独立拉起 proxy、等待 ready、复用 demo、驻留供前端联调、退出清理”这几步。
- 在 `server/tests/run_all_and_demo.sh` 的 `RUN_DIR`、`stop_process()`、`cleanup()`、`wait_for_http_ready()` 和 `hold_until_interrupt()` 附近补了中文注释，说明为什么要单独建临时目录、为什么先发 TERM、为什么 trap 阶段要关掉 `set -e`、为什么 readiness 直接打 HTTP、以及为什么 demo 跑完后不能立刻退出。

## Learning Tips

### Newbie Tips

联调脚本最容易踩的坑不是“起不来”，而是“起得来但关不干净”。只要脚本里有后台进程，就必须先想清楚谁记录 PID、谁负责回收、遇到 Ctrl+C 时会不会留下孤儿进程。

### Function Explanation

`mktemp -d`：创建唯一的临时目录。这里用它把数据库和日志都收口到同一轮运行目录里，既避免多次联调互相覆盖，也方便出错时直接看现场。

`trap cleanup EXIT INT TERM`：给 shell 绑定统一退出路径。既然脚本既可能正常结束，也可能被 Ctrl+C 或 kill 打断，那么清理逻辑就不能散落在多个分支里，应该集中在 trap 里做。

### Pitfalls

后端当前默认会自动拉起 proxy。如果联合脚本又手动起一份 proxy，但是忘了给后端传 `--no-auto-start-proxy`，那就不是“多一层保险”，而是两份进程争抢 8001，联调会直接变成端口冲突。

# 追加记录：2026-04-06 服务监控快照发布周期压到 1 秒

## Git Commit Message

perf(service-monitor): 将运行态快照发布周期压缩到1秒

## Modification

- `server/src/main.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/src/main.cpp` 的服务监控 `runEvery(...)` 附近补了中文注释，明确这次只是在答辩演示阶段把快照发布周期从 `5s` 压到 `1s`，减少额外等待。
- 同一段注释里也明确写了边界：分钟桶仍然按“封口后才进窗”，所以这次不会消除“第一次显示仍要等当前分钟结束”的问题。

## Learning Tips

### Newbie Tips

快照发布周期和统计粒度不是一回事。把快照从 `5s` 改成 `1s`，只能减少“已经有新快照但前端还没拿到”的等待；如果底层桶还是按 `1min` 封口，那么第一次显示仍然受分钟边界限制。

### Function Explanation

`loop.runEvery(1.0, ...)`：给事件循环注册一个固定周期定时任务。这里它不做重统计，只是驱动 `ServiceRuntimeAccumulator::OnTick()` 发布最新的窗口快照。

### Pitfalls

如果只盯着前端轮询周期，很容易误以为“页面慢就是前端刷得不够快”。这次专门把后端快照发布压到 `1s`，就是为了把“快照发布慢”和“分钟桶封口慢”这两个延迟拆开看，避免后面把锅甩错地方。

# 追加记录：2026-04-06 服务监控桶粒度改成 3 秒并开放启动参数

## Git Commit Message

feat(service-monitor): 将时间窗桶粒度改成3秒并支持启动参数

## Modification

- `server/core/ServiceRuntimeAccumulator.h`
- `server/core/ServiceRuntimeAccumulator.cpp`
- `server/src/main.cpp`
- `server/tests/ServiceRuntimeAccumulator_test.cpp`
- `server/tests/manual_service_monitor_demo.sh`
- `server/tests/run_all_and_demo.sh`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `server/core/ServiceRuntimeAccumulator.h` 里补了中文注释，把“分钟桶”统一收口成“可配置秒级桶”，说明为什么窗口桶数量要由“窗口总时长 / 桶粒度”自动算出来。
- 在 `server/core/ServiceRuntimeAccumulator.cpp` 里补了中文注释，重点解释 `OnPrimaryCommitted()` 为什么先写活跃桶、`OnTick()` 为什么只推进已封口桶，以及 `window_bucket_count_ + 1` 为什么能避免活跃桶和最老封口桶撞槽。
- 在 `server/src/main.cpp` 里补了 `--service-monitor-bucket-seconds` 的中文注释，说明它服务于答辩演示，不改“最近 30 分钟”这种产品语义。
- 在 `server/tests/ServiceRuntimeAccumulator_test.cpp` 里补了 3 秒桶测试的中文注释，锁死“3 秒封口后立即进窗”这个新行为。
- 在 `server/tests/manual_service_monitor_demo.sh` 和 `server/tests/run_all_and_demo.sh` 里补了环境变量注释，说明脚本为什么默认把桶粒度透传成 `3s`。

## Learning Tips

### Newbie Tips

窗口长度和桶粒度不是同一个参数。`30min` 决定“最近多长时间”，`3s` 决定“多久封口一次”。把它们拆开之后，才能既保留产品语义，又把演示延迟压下来。

### Function Explanation

`window_bucket_count = ceil(window_duration / bucket_granularity)`：窗口里需要保留多少个已封口桶，不应该再写死等于“窗口分钟数”，而应该按总时长和桶粒度动态换算。

### Pitfalls

如果只把桶粒度从 60 秒改成 3 秒，但还继续拿“窗口分钟数”直接当桶数量，那么窗口总覆盖时长会被错误压缩，退窗时间也会立刻跑偏。

# 追加记录：2026-04-06 服务监控手动刷新按钮与自动轮询解耦

## Git Commit Message

fix(frontend): 修复服务监控自动轮询长期占用刷新按钮状态

## Modification

- `client/src/views/ServiceMonitorPrototype.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260406-feat-service-runtime.md`

## 这次补了哪些注释

- 在 `client/src/views/ServiceMonitorPrototype.vue` 的 `fetchRuntimeSnapshot()` 附近补了中文注释，说明为什么“请求并发保护”和“手动按钮 loading”必须拆开。
- 在同文件的 `refreshRuntimeSnapshotManually()` 附近补了中文注释，说明手动刷新按钮只表达“这次点击触发的刷新”，不再被后台自动轮询长期占用。
- 在 `onMounted()` 的自动轮询注释里同步更新了后端 1 秒发布的新口径，并明确自动轮询只复用请求函数，不再接管按钮文案。

## Learning Tips

### Newbie Tips

自动刷新和手动刷新共用同一个请求函数没问题，但不要共用同一个“按钮文案状态”。否则后台轮询一触发，前台按钮就会一直显示“刷新中”，用户会直接以为页面卡了。

### Function Explanation

`runtimeRequestInFlight`：纯粹用来做请求并发保护，避免同一时刻叠两个 `/service-monitor/runtime` 请求。

`manualRefreshLoading`：只服务手动刷新按钮文案。它不代表后台有没有自动轮询请求，只代表“用户刚刚手点了一次刷新”。

### Pitfalls

如果只保留一个 `loading` 状态同时给“按钮文案”和“自动轮询防重入”用，那它的语义一定会串。前端交互里最容易出这种问题：代码能跑，但用户观感像坏掉了一样。
