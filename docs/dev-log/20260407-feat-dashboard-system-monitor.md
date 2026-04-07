# Git Commit Message

refactor(frontend): 收口系统监控页面骨架与文案

# Modification

- `client/src/views/Dashboard.vue`
- `client/src/components/TokenMetricsCard.vue`
- `client/src/i18n.ts`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

# 这次补了哪些注释

- 在 `client/src/views/Dashboard.vue` 里补了中文注释，说明这页当前先收口成“系统运行态页”，只保留系统指标卡、Token 卡和两条吞吐曲线。
- 在 `client/src/components/TokenMetricsCard.vue` 里补了中文注释，说明为什么去掉“节省比例 / 预估成本”，改成纯 token 真值展示。

# Learning Tips

## Newbie Tips

系统监控页和服务监控页不要混着做。前者看的是进程自身的运行态，例如队列等待、推理延迟、内存、背压和吞吐；后者看的是业务服务最近有没有出问题。两者如果混在一个页面里，用户会分不清自己到底是在看系统瓶颈还是业务异常。

## Function Explanation

`MetricCard` 这种通用卡片组件很适合先收语义、后接真值。也就是先把标题、副文案和布局收成你真正想要的系统口径，后面再把 `value` 换成后端快照，不要一边改文案一边改接口，容易把问题搅在一起。

## Pitfalls

“节省比例”“预估成本”这种文案在演示时很容易被追问计算口径。一旦当前页面还没有稳定的真实单价、provider 映射和 token 统计来源，这类卡片宁可先删，不要继续带着 mock 味很重的数字留在系统监控首页。

# 追加记录：2026-04-07 背压卡去掉 queue 百分比副文案

## Git Commit Message

feat(notification): 新增独立 webhook 手工联调入口

## Modification

- `server/tests/manual_webhook_notifier.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_WebhookNotifier.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/tests/manual_webhook_notifier.cpp` 里补了中文注释，说明为什么这条入口故意只构造假 `TraceAlertEvent`，以及为什么它要和主链、Settings、数据库解耦。
- 在 `server/CMakeLists.txt` 里补了中文注释，说明 `manual_webhook_notifier` 是手工联调目标，不注册进 CTest，避免自动化测试误发真实外部消息。

## Learning Tips

### Newbie Tips

真实 webhook 联调入口最好和主程序入口分开。这样出问题时，变量只剩消息模板、HTTP 发送和平台返回，不会把 Settings、生效逻辑、数据库配置一起卷进来。

### Function Explanation

这次新增的 `manual_webhook_notifier` 不是单元测试，而是一个独立的手工联调可执行文件。它通过命令行参数构造一条假的 `TraceAlertEvent`，然后直接调用 `WebhookNotifier::notifyTraceAlert`，专门用来验证飞书等外部 webhook 的真实送达链路。

### Pitfalls

不要把真实外部 webhook 联调入口注册进 CTest。否则你一旦执行 `ctest` 或 CI 跑全量测试，就可能把真实告警消息发到外部群里，测试环境和生产联调边界会立刻变脏。

# 追加记录：2026-04-07 Dashboard 前端切到系统运行态真快照

## Git Commit Message

refactor(frontend): 移除系统监控背压卡的队列百分比副文案

## Modification

- `client/src/views/Dashboard.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `client/src/views/Dashboard.vue` 的背压状态计算附近补了中文注释，说明为什么这张卡现在只显示综合状态，不再附带单一队列百分比。

## Learning Tips

### Newbie Tips

如果后端某个状态是“多因素联合判断”出来的结论，前端就不要再随手拿其中一个局部因子当副文案。否则页面会出现一种很怪的情况：主标题说的是综合状态，副文案却像在暗示它只由一个队列决定。

### Function Explanation

这里删掉的是 `MetricCard` 的 `subValue`，不是把背压卡整个删掉。主值仍然来自 `backpressureStatus`，只是把容易误导的补充说明拿掉。

### Pitfalls

`queuePercent` 这个字段当前仍可能保留在 store 或旧接口里，但这不代表它适合继续展示。展示层应该优先服从当前页面语义，而不是为了“字段已经有了”就硬塞进 UI。

# 追加记录：2026-04-07 Trace AI usage 回传与 token 真值入口

## Git Commit Message

feat(ai): 补齐 Trace AI usage 回传并接入告警 token 真值口径

## Modification

- `server/ai/AiTypes.h`
- `server/ai/TraceAiProvider.h`
- `server/ai/TraceProxyAi.h`
- `server/ai/TraceProxyAi.cpp`
- `server/ai/MockTraceAi.h`
- `server/ai/MockTraceAi.cpp`
- `server/ai/proxy/providers/base.py`
- `server/ai/proxy/providers/gemini.py`
- `server/ai/proxy/main.py`
- `server/core/TraceSessionManager.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/tests/TraceSessionManager_integration_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/ai/AiTypes.h` 里补了中文注释，说明为什么把 `analysis` 和 `usage` 拆开，以及 `TraceAiUsage/TraceAiResponse` 的职责边界。
- 在 `server/ai/TraceAiProvider.h` 里补了中文注释，说明 Trace AI 接口为什么不再返回裸 `LogAnalysisResult`。
- 在 `server/ai/TraceProxyAi.h` 和 `server/ai/TraceProxyAi.cpp` 里补了中文注释，说明 proxy 外层 `usage` 的解析顺序、缺失时为什么允许回退、不把它当协议错误。
- 在 `server/ai/MockTraceAi.h` 和 `server/ai/MockTraceAi.cpp` 里补了中文注释，说明 mock 路径为什么故意不返回 usage，用来继续覆盖本地估算回退分支。
- 在 `server/ai/proxy/providers/gemini.py` 和 `server/ai/proxy/main.py` 里补了中文注释，说明 Gemini 官方 `usage_metadata` 怎么归一化成内部统一字段，以及为什么要把 `usage` 放在外层 sibling 字段。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明告警 token_count 为什么优先吃 provider usage、什么时候回退到本地估算、为什么这一步不去回写数据库主记录。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 和 `server/tests/TraceSessionManager_integration_test.cpp` 里补了中文注释，说明单测锁的是“通知事件 token 口径”，以及 duplicate span / summary risk_level 的当前真实语义。

## Learning Tips

### Newbie Tips

第三方模型的 token 统计不要直接猜字段名。即使都是“用量统计”，Gemini 也更偏 `prompt_token_count / candidates_token_count / total_token_count`，而不是很多人下意识写死的 `input_tokens / output_tokens`。工程上应该先按 provider 原生字段读，再在自己系统里做归一化。

### Function Explanation

这次新增的 `TraceAiResponse` 不是为了“更优雅”，而是为了把两类不同生命周期的数据拆开：`analysis` 是业务结果，给 trace 分析和前端详情页用；`usage` 是计量元数据，给系统监控、告警 token 口径、后续 prompt_debug 用。两者如果继续混成一个 JSON 树，后面每个消费者都得自己二次拆字段。

### Pitfalls

不要为了省一个局部变量，就把“临时 token_count”塞进 `confidence` 或别的无关字段桥接。那种写法短期能跑，长期一定把语义搞脏，后面谁读到这个字段都会被误导。正确做法是明确保留一个单独的局部状态，或者等真正需要持久化时再扩正式结构。

# 追加记录：2026-04-07 SystemRuntimeAccumulator 第一刀（骨架 + TDD）

## Git Commit Message

feat(core): 新增系统监控运行态累加器骨架

## Modification

- `server/core/SystemRuntimeAccumulator.h`
- `server/core/SystemRuntimeAccumulator.cpp`
- `server/tests/SystemRuntimeAccumulator_test.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/core/SystemRuntimeAccumulator.h` 里补了中文注释，说明 `overview / token_stats / timeseries` 三块快照各自服务哪一片系统监控 UI，以及为什么 `RecordAcceptedLogs / RecordAiCompletion / OnTick` 这样拆入口。
- 在 `server/core/SystemRuntimeAccumulator.cpp` 里补了中文注释，说明为什么速率采样坚持“单调总数做差分”、为什么两张延迟卡吃固定样本平均、为什么 `OnTick()` 里顺手采 RSS 而不是在热路径读系统信息。
- 在 `server/tests/SystemRuntimeAccumulator_test.cpp` 里补了中文注释，说明四条测试分别锁空快照、累计值+平均值、单调差分速率、固定样本窗口这四个口径。

## Learning Tips

### Newbie Tips

系统监控和服务监控虽然都叫“运行态”，但后端模型完全不一样。服务监控更像“业务事件进窗再退窗”；系统监控更像“原子累计 + 当前状态 + 定时采样”，别一上来把两者硬套成同一套分钟桶。

### Function Explanation

`std::atomic<T>` 这里主要用在“单调累计”和“当前状态覆盖”两类值上，所以第一版直接用 `memory_order_relaxed` 就够了。因为这里不靠某个原子变量去发布别的复杂对象，也不拿它当同步门闩，只需要数值最终稳定可读。

### Pitfalls

系统吞吐图最容易犯的错是“每秒把总数清零再读”。这种写法表面直观，实际会把热路径写入和采样线程绑在一起。更稳的做法是一直维护单调总数，定时器只做差分采样，这样写路径就只剩 `fetch_add`，不会引入额外的清零同步。

# 追加记录：2026-04-07 SystemRuntimeAccumulator 口径细化（AI started/completed + 复合延迟样本）

## Git Commit Message

refactor(core): 细化系统监控 AI 调用成熟时机与延迟样本口径

## Modification

- `server/core/SystemRuntimeAccumulator.h`
- `server/core/SystemRuntimeAccumulator.cpp`
- `server/tests/SystemRuntimeAccumulator_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/core/SystemRuntimeAccumulator.h` 里补了中文注释，说明为什么把 `RecordAiCallStarted()` 和 `RecordAiCallCompleted()` 拆开，以及为什么“排队等待 + 推理耗时”要作为同一次 AI 调用的一条复合样本推进窗口。
- 在 `server/core/SystemRuntimeAccumulator.cpp` 里补了中文注释，说明为什么完成态样本在一次短锁里统一写入，而不是把两张延迟卡拆成两套独立窗口。
- 在 `server/tests/SystemRuntimeAccumulator_test.cpp` 里补了中文注释，说明新增测试锁的是“调用已发起”和“调用已完成”不是同一成熟时机。

## Learning Tips

### Newbie Tips

“已经发起了多少次 AI 调用”和“已经完成了多少次 AI 调用”看起来只差一个单词，但它们在系统监控里是两种完全不同的信号。前者更像入口压力，后者更像实际吞吐；如果把两者一直混成一个数，后面遇到队列堆积或超时时就看不出问题到底卡在哪一段。

### Function Explanation

这次把延迟样本从“两套独立数值窗口”改成了“一条样本里同时带 queue_wait_ms 和 inference_latency_ms”。这样做不是为了省变量，而是为了保留“它们来自同一次 AI 调用”这层语义，后面如果要扩展更多阶段耗时，也能继续沿着这个样本结构往里加。

### Pitfalls

如果一上来就把两张延迟卡拆成两套完全独立的统计路径，短期看也能出平均值，但后面一旦想加更多阶段、更多解释字段，语义就会越来越散。复合样本的价值就在于：一次调用产生的多个阶段耗时，始终作为一个整体被记录和理解。

# 追加记录：2026-04-07 SystemMonitor 主链接线（先接埋点，不切 dashboard）

## Git Commit Message

feat(core): 接入系统监控主链埋点与背压状态同步

## Modification

- `server/handlers/LogHandler.h`
- `server/handlers/LogHandler.cpp`
- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `server/tests/LogHandler_test.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/handlers/LogHandler.h` 里补了中文注释，说明为什么系统监控累加器保持可选注入、当前只负责埋点不参与 Trace 主链判断。
- 在 `server/handlers/LogHandler.cpp` 里补了中文注释，说明为什么 `/logs/spans` 成功接收后就该记录总处理日志数，而不是等 trace 最终聚合完成。
- 在 `server/core/TraceSessionManager.h` 里补了中文注释，说明 `SystemRuntimeAccumulator` 的职责边界，以及它为什么不该反向驱动 TraceSessionManager 状态机。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明为什么 `RecordAiCallStarted()` 记在真正调模型前、为什么 `queue_wait_ms + inference_latency_ms` 要在 AI 收尾后一次性作为复合样本提交、为什么背压状态要做内部状态到前端三档结论的映射。
- 在 `server/src/main.cpp` 里补了中文注释，说明这一步只先把系统监控埋点和定时采样接进主链，还没有切 `/dashboard` 读取入口。
- 在 `server/tests/LogHandler_test.cpp` 里补了中文注释，说明 `/logs/spans` 成功接收和“trace 已经真正 dispatch/聚合完成”不是同一个成熟时机。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，说明 started/completed/token/backpressure 这几条系统监控口径分别锁什么。

## Learning Tips

### Newbie Tips

系统监控里的很多数字看起来都是“总数”，但成熟时机并不一样。`ai_call_total` 表达的是“真正开始调模型了多少次”，`ai_completion_total` 表达的是“真正完成了多少次调用”；如果两者都在同一个时间点加一，后面根本看不出队列堆积、调用失败或中途回滚。

### Function Explanation

这次在 `TraceSessionManager` 里新增的是“埋点接线”，不是业务语义改造。也就是说：Push/Seal/Dispatch/AI/Alert 的行为没有换，只是在已有时间线的关键节点上，把系统监控需要的 started/completed/token/backpressure 信息挂出去。

### Pitfalls

`/logs/spans` 成功返回 `202` 只能说明“入口已经接住这条 span”，不能说明后面的 sealed grace、worker submit、AI 调用都已经成功。所以系统监控里的“总处理日志数”和 Trace 聚合是否最终成功是两回事，测试也要按这两个成熟时机分开锁，不能混成一条断言。

# 追加记录：2026-04-07 Dashboard 读源切换到系统运行态快照

## Git Commit Message

refactor(handler): 让 dashboard 直接读取系统运行态快照

## Modification

- `server/handlers/DashboardHandler.h`
- `server/handlers/DashboardHandler.cpp`
- `server/src/main.cpp`
- `server/CMakeLists.txt`
- `server/tests/DashboardHandler_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/handlers/DashboardHandler.h` 里补了中文注释，说明为什么 dashboard 不再依赖 SQLite 和线程池，而是直接读 `SystemRuntimeAccumulator` 快照。
- 在 `server/handlers/DashboardHandler.cpp` 里补了中文注释，说明这一步为什么要同步返回内存快照、为什么这一刀只切读源不顺手扩字段。
- 在 `server/src/main.cpp` 里补了中文注释，说明 `/dashboard` 现在正式切到系统运行态快照，不再绕回旧的数据库统计。
- 在 `server/tests/DashboardHandler_test.cpp` 里保留并补充了中文注释，说明这条测试锁的是“handler 已经不再返回旧 dashboard 结构，而是直接吐 overview/token_stats/timeseries”。

## Learning Tips

### Newbie Tips

如果一个页面要展示的是内存里的实时运行态，就不要再习惯性绕回数据库。数据库更适合查历史结果、分页列表、已落库事实；实时状态页更适合直接读进程内快照，这样延迟低，语义也不会被历史数据拖脏。

### Function Explanation

这次 `DashboardHandler` 从“SQLite 查询 + 线程池异步回发”改成了“同步读取内存快照再直接返回”。原因不是图省事，而是系统监控快照本来就已经在内存里，并且 `BuildSnapshot()` 本身已经把需要的累计值、当前值和折线图点整理好了，再丢进线程池只会多一层无意义搬运。

### Pitfalls

从旧 handler 切到新快照时，最容易漏的是测试和 CMake。只改 `DashboardHandler.cpp` 不把新测试 target 挂进 `CMakeLists.txt`，编译阶段根本不会帮你兜住“接口已经换了，但没人验证 JSON 结构”的问题。

# 追加记录：2026-04-07 Dashboard 前端切到系统运行态真快照

## Git Commit Message

feat(frontend): 让系统监控页面读取真实运行态快照

## Modification

- `client/src/stores/system.ts`
- `client/src/views/Dashboard.vue`
- `client/src/components/TokenMetricsCard.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `client/src/stores/system.ts` 里补了中文注释，说明 dashboard 为什么从旧 `DashboardStatsResponse` 切到 `overview / token_stats / timeseries` 三段快照，以及为什么前端不再自己生成假图表和假内存值。
- 在 `client/src/views/Dashboard.vue` 里补了中文注释，说明顶部卡片已经直接读取真实 runtime stats、为什么内存卡改成 `RSS MB`、以及为什么背压卡不再附带单队列百分比。
- 在 `client/src/components/TokenMetricsCard.vue` 里补了中文注释，说明 token 卡为什么不再维护本地 mock，而是直接读取 store 里的系统运行态真值。

## Learning Tips

### Newbie Tips

前端接真接口时，最容易犯的错不是字段名改漏，而是“接口已经变了，页面却还在偷偷补假数据”。这样页面表面能动，实际上你根本分不清哪个数是真值、哪个数是前端自己编的。系统监控这类页面要么吃真快照，要么明确空态，最忌讳真假混用。

### Function Explanation

这次 `system.ts` 里保留了 `MetricPoint { time, qps, aiRate }` 这层前端结构，但后端返回的是 `timeseries[].{time_ms, ingest_rate, ai_completion_rate}`。前端没有直接把 `time_ms` 当真实时间展示，而是按点的顺序回填 `HH:mm:ss` 标签，因为当前后端的 `time_ms` 还是内部采样时钟，不是用户能理解的墙上时间。

### Pitfalls

内存卡如果继续沿用旧的 `memoryPercent + 进度条`，页面看起来会像“宿主机资源面板”，但后端现在拿到的其实是进程 RSS。进程内存和整机百分比不是一个量纲，前端如果不跟着改口径，用户看到的就是一张表面正常、实际语义完全错位的卡片。

# 追加记录：2026-04-07 SystemRuntimeAccumulator 改成 OnTick 发布快照

## Git Commit Message

refactor(core): 让系统监控快照在 OnTick 预构建并发布

## Modification

- `server/core/SystemRuntimeAccumulator.h`
- `server/core/SystemRuntimeAccumulator.cpp`
- `server/handlers/DashboardHandler.cpp`
- `server/tests/SystemRuntimeAccumulator_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/core/SystemRuntimeAccumulator.h` 里补了中文注释，说明 `BuildSnapshot()` 为什么不再现场拼装，而是只返回上一次 `OnTick` 已经发布好的成品快照。
- 在 `server/core/SystemRuntimeAccumulator.cpp` 里补了中文注释，说明为什么把快照构建时机前移到 `OnTick()`、为什么请求线程只读已发布对象、以及这和降低读路径锁竞争的关系。
- 在 `server/handlers/DashboardHandler.cpp` 里补了中文注释，说明 `/dashboard` 现在读取的是 `OnTick` 已经发布好的内存快照，不再在请求线程现场拼 `overview/token/timeseries`。
- 在 `server/tests/SystemRuntimeAccumulator_test.cpp` 里补了中文注释，说明新增测试锁的是“未经过 OnTick 发布前，请求侧仍然只能看到上一版快照”。

## Learning Tips

### Newbie Tips

“定时器采样”和“HTTP 请求返回”虽然都能拿到同一份数据，但不代表应该在两个地方都做一遍同样的拼装工作。只要页面轮询节奏和后端采样节奏接近，把快照构建固定放到 `OnTick()` 里，读路径就会更轻，锁竞争也更容易收敛。

### Function Explanation

这次用到的是 `std::shared_ptr<const SystemRuntimeSnapshot>` 配合 `std::atomic_store_explicit / std::atomic_load_explicit`。写侧在 `OnTick()` 里生成一份新的成品快照，然后用 `release` 语义发布；读侧用 `acquire` 语义拿到当前快照指针后再拷一份返回。这样请求线程不用自己再去摸那些样本窗口和折线图历史。

### Pitfalls

如果只改了实现，不补“失败测试先锁发布语义”，很容易把“OnTick 发布快照”和“请求时现场拼快照”两种行为混着存在，表面看页面都能出数，但锁竞争、时序和测试口径会越来越乱。先让测试明确区分“已发布”和“未发布”两个状态，后面的重构才不会偷跑偏。

# 追加记录：2026-04-07 ServiceRuntimeAccumulator 改成原子发布快照

## Git Commit Message

refactor(core): 让服务监控快照在 OnTick 原子发布

## Modification

- `server/core/ServiceRuntimeAccumulator.h`
- `server/core/ServiceRuntimeAccumulator.cpp`
- `server/handlers/ServiceMonitorHandler.cpp`
- `server/tests/ServiceRuntimeAccumulator_test.cpp`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/core/ServiceRuntimeAccumulator.h` 里补了中文注释，说明 `BuildSnapshot()` 现在为什么只返回 `OnTick()` 已经原子发布好的成品快照。
- 在 `server/core/ServiceRuntimeAccumulator.cpp` 里补了中文注释，说明为什么服务监控也把“窗口推进 + topk 排序 + 快照发布”统一收口到 `OnTick()`，以及为什么请求线程不该再进窗口锁。
- 在 `server/handlers/ServiceMonitorHandler.cpp` 里补了中文注释，说明 `/service-monitor/runtime` 现在读取的是已经发布好的内存快照，不再现场参与排序和裁切。
- 在 `server/tests/ServiceRuntimeAccumulator_test.cpp` 里补了中文注释，说明新增测试锁的是“analysis 虽然已经写进最近态内部状态，但在下一次 `OnTick()` 发布前，请求侧仍然只能看到旧快照”。

## Learning Tips

### Newbie Tips

服务监控这类页面比系统监控更适合走“定时推进 -> 定时发布 -> 请求只读”的模式。因为它本来就有时间窗、退窗、服务榜和操作榜排序，如果再让 HTTP 请求线程现场拼装一次，锁竞争和时序口径都会越来越乱。

### Function Explanation

这次服务监控也用到了 `std::shared_ptr<const ServiceRuntimeSnapshot>` 配合 `std::atomic_store_explicit / std::atomic_load_explicit`。写侧在 `OnTick()` 持锁推进完窗口后统一生成成品快照，再用 `release` 语义发布；读侧用 `acquire` 语义拿当前指针后直接拷一份返回。这样请求线程不会再碰 `window_services_ / recent_services_ / global_operations_` 这些内部真相状态。

### Pitfalls

如果只把 `published_snapshot_` 换成原子指针，却不补“下一次 `OnTick()` 发布前仍应返回旧快照”的测试，那么很容易又把 `OnAnalysisReady()` 之类的内部状态变化泄漏到请求路径上。页面表面还能出数，但“已发布快照”和“内部最新状态”两套语义会重新混在一起。

# 追加记录：2026-04-07 WebhookNotifier 增加 formatter 分层并接入飞书 post 模板

## Git Commit Message

feat(notification): 为 webhook 通知增加飞书 formatter 分层

## Modification

- `server/notification/NotifyTypes.h`
- `server/notification/WebhookFormatters.h`
- `server/notification/WebhookFormatters.cpp`
- `server/notification/WebhookNotifier.h`
- `server/notification/WebhookNotifier.cpp`
- `server/tests/WebhookNotifier_test.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_WebhookNotifier.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/notification/NotifyTypes.h` 里补了中文注释，说明 `WebhookChannel` 为什么先只收口成 `provider/webhook_url/enabled` 三个字段。
- 在 `server/notification/WebhookFormatters.h` 里补了中文注释，说明 formatter 层只负责“领域事件 -> 平台 payload”，不负责 HTTP 发送。
- 在 `server/notification/WebhookFormatters.cpp` 里补了中文注释，说明飞书第一版为什么先收口成最小 `post` 模板，以及 provider 未知时为什么统一回退到 generic formatter。
- 在 `server/notification/WebhookNotifier.h` 里补了中文注释，说明为什么保留旧 `vector<string>` 构造函数，以及测试用 `PostJsonFn` 为什么只用于隔离网络副作用。
- 在 `server/notification/WebhookNotifier.cpp` 里补了中文注释，说明为什么 `notifyTraceAlert()` 现在先按 `enabled` 过滤，再按 `provider` 选 formatter，最后统一走同一套 `cpr` 发送逻辑。
- 在 `server/tests/WebhookNotifier_test.cpp` 里补了中文注释，说明两条测试分别锁飞书 `post` 模板和 `enabled/provider` 分流语义。

## Learning Tips

### Newbie Tips

这里最容易走歪的是“一看到平台差异就急着写一堆 notifier 子类”。如果真正变化的只有 payload 结构，而 HTTP 发送流程、超时、错误日志都一样，那更适合拆 formatter，而不是先堆大继承树。

### Function Explanation

这次 `WebhookNotifier` 里新增的 `PostJsonFn` 不是为了业务层可配置，而是为了测试隔离网络副作用。生产环境下它为空，代码继续走 `thread_local cpr::Session`；测试里才注入一个 lambda，把 `channel/body/log_prefix` 记下来断言，不必真起 HTTP 服务。

### Pitfalls

如果只在 `WebhookNotifier` 里加 `if (provider == "feishu")`，短期也能跑，但后面一旦要补签名、钉钉或 generic 老协议兼容，平台差异会很快把发送逻辑和 payload 逻辑搅在一起。先把 formatter 边界立住，后面再接 Settings 或真实飞书联调时，改动面才不会继续扩散。

# 追加记录：2026-04-07 WebhookNotifier 补齐 HTTPS 支持与 transport error 日志

## Git Commit Message

fix(notification): 打开 webhook https 支持并补全传输层错误日志

## Modification

- `server/CMakeLists.txt`
- `server/notification/WebhookNotifier.cpp`
- `docs/todo-list/Todo_WebhookNotifier.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/CMakeLists.txt` 里补了中文注释，说明真实飞书 webhook 走的是 `https`，不能继续把 `cpr/curl` 编成 `HTTP-only`。
- 在 `server/notification/WebhookNotifier.cpp` 里补了中文注释，说明为什么 `status=0` 这类失败必须把 `cpr::Error` 的 `code/message` 一起打出来，单看 `status/text` 不足以定位 TLS、DNS、连接失败这类传输层问题。

## Learning Tips

### Newbie Tips

`HTTP 200/400/500` 这类状态码只覆盖“请求已经发到对端并拿到了 HTTP 响应”的场景。像 TLS 握手失败、DNS 失败、连接被拒绝这种问题，很多 HTTP 客户端会直接给你一个 `status=0`，真正能说明问题的是底层 transport error。

### Function Explanation

这次 `WebhookNotifier` 打出来的 `ErrorCode/ErrorMsg` 来自 `cpr::Response.error`。`status_code` 表示 HTTP 层结果，`error.code/error.message` 表示请求在更底层的发送过程中有没有失败。两者不是同一层语义，调外部 webhook 时必须一起看。

### Pitfalls

如果一开始为了简化依赖把 `cpr/curl` 编成 `HTTP-only`，那本地 mock webhook 看起来都能跑，但一旦接真实平台（飞书、Slack、钉钉），只要对方要求 `https`，你就会得到一种很迷惑的现象：程序没崩、payload 也像对的，但消息永远到不了。这个坑不把 TLS 支持打开，前面调模板全是白费。

# 追加记录：2026-04-07 主程序增加临时 webhook 直连入口

## Git Commit Message

feat(server): 为主程序增加临时 webhook 直连启动参数

## Modification

- `server/src/main.cpp`
- `docs/todo-list/Todo_WebhookNotifier.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/src/main.cpp` 的参数解析里补了中文注释，说明为什么先增加临时 `--webhook-provider/--webhook-url`，而不是直接把 Settings 这条线一起接上。
- 在 `server/src/main.cpp` 的 notifier 初始化处补了中文注释，说明为什么启动时直接把临时参数映射成 `WebhookChannel`，以及为什么它要和本地 mock webhook 共存。

## Learning Tips

### Newbie Tips

当你要把一个真实外部能力接进主程序时，先给主程序一个“临时直连入口”通常比立刻接完整配置系统更稳。这样能先证明主链、消息模板和外部平台本身都是通的，再回头接 Settings，问题定位会轻松很多。

### Function Explanation

这次主程序新增的 `--webhook-provider` 和 `--webhook-url` 只负责启动时临时构造一个 `WebhookChannel`。它们不会回写数据库，也不会改变 Settings 的最终设计，只是给 `WebhookNotifier` 提供一个短路径，让 `critical trace -> 外部 webhook` 这条链能先真实跑起来。

### Pitfalls

如果只加 `--webhook-url` 不加 `--webhook-provider`，或者反过来只传 provider 不传 url，启动时应该立刻失败，而不是默默退回默认值。因为这类参数一旦半残，会让你以为“主链没有触发通知”，实际上只是初始化阶段配置就已经脏了。

# 追加记录：2026-04-07 随机 trace 发数脚本增加 critical 模式

## Git Commit Message

feat(test): 为随机 trace 发数脚本增加 critical 产出模式

## Modification

- `server/tests/post_random_trace_once.py`
- `server/tests/post_random_trace_once_test.py`
- `docs/todo-list/Todo_WebhookNotifier.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/tests/post_random_trace_once.py` 里补了中文注释，说明为什么 `force_critical` 要显式往 trace 文本里塞 `critical` 标记，以及这样做和 mock trace AI 风险判定的关系。
- 在 `server/tests/post_random_trace_once_test.py` 里补了中文注释，说明这条测试锁的是“脚本能不能稳定产出 critical 语义”，不是网络发送结果。

## Learning Tips

### Newbie Tips

如果下游风险判定是基于文本关键词的 mock 逻辑，那联调脚本就不能只靠“有几个 ERROR span”去赌它会不会变成 `critical`。最稳的做法是给脚本一个明确开关，让它在需要时显式注入 `critical` 标记，这样测试和演示都可控。

### Function Explanation

这次新增的 `--critical` 开关不会改脚本默认行为。默认仍然发送原来的复杂异常 trace；只有显式传 `--critical` 时，脚本才会把 `bank/payment/root` 这几个位置的属性改成带 `critical` 的文本，从而让 mock trace AI 在 `analyze_trace()` 里稳定命中 `risk_level = critical`。

### Pitfalls

只改脚本帮助文案、不补一条最小测试是很危险的。因为这种“通过关键词触发风险等级”的逻辑很脆，后面哪怕有人把属性名、字段值或序列化文本改了，脚本表面上还能发送成功，但 webhook 就再也不会触发。先用测试锁住“critical 字样必须真的出现在生成 payload 里”，后面才不容易悄悄回退成 `error`。

# 追加记录：2026-04-07 飞书 webhook 增加可选签名支持

## Git Commit Message

feat(notification): 为飞书 webhook 增加可选签名校验支持

## Modification

- `server/notification/NotifyTypes.h`
- `server/notification/WebhookNotifier.h`
- `server/notification/WebhookNotifier.cpp`
- `server/tests/WebhookNotifier_test.cpp`
- `server/tests/manual_webhook_notifier.cpp`
- `server/src/main.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_WebhookNotifier.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `server/notification/NotifyTypes.h` 里补了中文注释，说明 `WebhookChannel.secret` 是飞书签名用的共享密钥，不是公私钥体系里的私钥。
- 在 `server/notification/WebhookNotifier.h` 和 `server/notification/WebhookNotifier.cpp` 里补了中文注释，说明为什么签名放在发送前包装层而不是 formatter 里，以及 `timestamp/sign` 的计算时机和职责边界。
- 在 `server/src/main.cpp` 和 `server/tests/manual_webhook_notifier.cpp` 里补了中文注释，说明 `--webhook-secret` 为空和非空时的行为差异，以及为什么它先走临时直连入口而不是 Settings。
- 在 `server/tests/WebhookNotifier_test.cpp` 里补了中文注释，说明新增测试锁的是“secret 存在时必须自动补 timestamp/sign”，而不是只检查字段有没有被随便塞进去。
- 在 `server/CMakeLists.txt` 里通过显式链接 `OpenSSL::Crypto` 把签名依赖收口到通知模块，避免后续靠传递依赖侥幸编过。

## Learning Tips

### Newbie Tips

飞书自定义机器人的“签名校验”不是公私钥，也不是你把 secret 原样塞进请求体。它本质上是共享密钥 HMAC：本地和飞书后台都拿同一个 secret，根据 `timestamp + "\n" + secret` 算一遍 HMAC-SHA256，再比对结果。

### Function Explanation

这次 `WebhookNotifier` 里新增的是“发送前包装层”：formatter 先负责业务消息体 `msg_type/content`，如果渠道是飞书且 `secret` 非空，notifier 再在最终发出的 JSON 外层补 `timestamp` 和 `sign`。这样平台消息模板和平台安全协议不会重新搅在一起。

### Pitfalls

签名这类功能如果不注入固定时间源，测试很容易变成“不知道为什么有时过有时不过”。这次给 `WebhookNotifier` 加 `NowSecondsFn`，就是为了在测试里把时间戳钉死，确保 `sign` 可以做精确断言，而不是退化成“只要字段存在就算通过”这种没意义的测试。

# 追加记录：2026-04-07 前端导航收口（隐藏 Live Logs / Benchmark）

## Git Commit Message

refactor(frontend): 收口前端导航并隐藏未完成页面入口

## Modification

- `client/src/router/index.ts`
- `client/src/layout/MainLayout.vue`
- `docs/todo-list/Todo_Frontend_Refactoring.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `client/src/router/index.ts` 里补了中文注释，说明为什么 `/logs` 和 `/benchmark` 不直接删除，而是先重定向到稳定页面。
- 在 `client/src/layout/MainLayout.vue` 里补了中文注释，说明为什么侧边栏现在只保留已经联调验收过的主链页面入口。

## Learning Tips

### Newbie Tips

收口半成品页面时，优先做“隐藏入口 + 保留兜底重定向”，不要一上来就物理删文件。因为答辩前最怕的是演示路径稳定性被你自己删坏，而不是仓库里多留两个暂时不用的页面文件。

### Function Explanation

Vue Router 里的 `redirect` 适合拿来做这种“旧地址还存在，但不再作为正式入口”的场景。这样历史书签、浏览器地址栏手输、甚至别人口头记错路径时，都不会直接落到半成品页面。

### Pitfalls

如果你只删侧边栏，不处理旧路由，那么用户仍然可以靠地址栏直接进入页面，这种“表面隐藏、实际还能进”的状态在演示时最容易翻车。所以导航收口和路由收口必须一起做。
