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
