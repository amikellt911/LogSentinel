## Git Commit Message

feat(trace-ai): 接通熔断冷启动配置与 skipped_circuit 状态

## Modification

- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260410-feat-ai-circuit-breaker.md`

## 这次补了哪些注释

- 在 `server/core/TraceSessionManager.h` 里补了中文注释，说明熔断参数为什么继续按冷启动配置消费，以及为什么当前只保留“失败阈值 + 冷却时间”这组最小状态。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明 `skipped_circuit` 不是一次新的 provider 失败，而是保护性短路；同时说明成功后为什么要清零连续失败数。
- 在 `server/src/main.cpp` 里补了中文注释，说明 `ai_circuit_breaker/ai_failure_threshold/ai_cooldown_seconds` 现在已经真实进入 `TraceSessionManager` 构造参数，而不是停留在 Settings 假保存。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，锁定了“达到阈值后跳过 AI”和“冷却过后重新放行”的两条熔断语义。

## Learning Tips

### Newbie Tips

熔断的本质不是“报错时打印一条日志”，而是“在系统知道下游大概率还没恢复时，主动停止继续打它”。所以它至少要回答两个问题：什么时候开始拦截，什么时候重新放行。当前这刀里，前者由 `consecutive_failures >= threshold` 决定，后者由 `open_until_ms > now_ms` 决定。

### Function Explanation

`IsAiCircuitOpen` 只做一件事：判断当前时间是否仍处在开路窗口内。`RecordAiCircuitFailure` 只做一件事：累计失败并在达到阈值时写下“开路到什么时候”。这两个函数拆开后，worker 分支就比较干净，不会把“是否该调 AI”和“失败后如何记账”揉在一块。

### Pitfalls

不要把 `skipped_circuit` 也当成一次新的失败去继续 `count++`。如果你这么做，熔断窗口里的每条 trace 都会继续推高失败计数，等冷却结束后系统会更难恢复，语义也会变成“我明明没去调 provider，怎么还在记失败”。

## Verification

- `cmake --build server/build --target test_trace_session_manager_unit`
- `./server/build/test_trace_session_manager_unit --gtest_filter='TraceSessionManagerUnitTest.DispatchSkipsAiWhenCircuitBreakerIsOpen:TraceSessionManagerUnitTest.DispatchRetriesAiAfterCircuitCooldownExpires:TraceSessionManagerUnitTest.DispatchStoresNormalizedProxyFailureMessageAsAiError'`
- `cmake --build server/build --target LogSentinel`

## Verification Result

- `test_trace_session_manager_unit`：编译通过
- 3 条熔断相关语义测试：3/3 通过
- `LogSentinel`：链接通过

---

## Git Commit Message

feat(trace-ai): 接通自动降级主备 provider 切换链路

## Modification

- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260410-feat-ai-circuit-breaker.md`

## 这次补了哪些注释

- 在 `server/core/TraceSessionManager.h` 里补了中文注释，说明 `fallback_trace_ai_` 和 `ai_auto_degrade_enabled_` 的职责边界，以及它和熔断不是一回事。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明为什么自动降级只在主路失败后才触发，以及为什么主备都失败时要把两段错误一起收口进 `ai_error`。
- 在 `server/src/main.cpp` 里补了中文注释，说明 fallback provider/model/api_key 也按冷启动构造，避免把动态切换逻辑塞进 `TraceProxyAi`。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，锁定“主路失败但 fallback 成功仍应视为 completed”和“主备都失败要落 `failed_both`”这两条语义。

## Learning Tips

### Newbie Tips

自动降级和熔断是两层不同的问题。自动降级回答的是“主路这次挂了，要不要立刻换一条备路再试一次”；熔断回答的是“我最近连续失败太多，接下来这一小段时间是不是整条 AI 链都别打了”。这两个概念如果揉在一起，后面状态机会越来越乱。

### Function Explanation

`CreateTraceAiProvider` 现在还是一个“单次 provider 对象工厂”，它只负责按照 backend/model/api_key 造出一条固定调用链。真正的主备切换放在 `TraceSessionManager`，这样 provider 层不用承担“先试 A 再试 B”的流程控制。

### Pitfalls

主路失败、fallback 成功时，不要把状态记成 `failed_primary`。一旦你这么做，前端就会把这条 trace 误当成真正分析失败，但数据库里其实已经落了 analysis，语义直接打架。正确做法是：成功就继续走 `completed`，只有主备都失败时才写 `failed_both`。

## Verification

- `cmake --build server/build --target test_trace_session_manager_unit LogSentinel`
- `./server/build/test_trace_session_manager_unit`

## Verification Result

- `test_trace_session_manager_unit`：44/44 通过
- 新增自动降级两条语义测试：2/2 通过
- `LogSentinel`：链接通过
