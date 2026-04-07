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
