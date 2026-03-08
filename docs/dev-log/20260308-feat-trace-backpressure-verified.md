# 20260308-feat-trace-backpressure-verified

feat(trace): 实装 Trace 背压拦截逻辑并补齐核心回归单测

## Modification
- **server/core/TraceSessionManager.cpp**: 
    - 在 `Push` 入口处正式调用 `RefreshOverloadState` 与 `ShouldRejectIncomingTrace` 准入拦截。
    - 确保 `Overload` 状态下仅拒绝新 Trace (503)，已存在的 Trace 允许继续补充 Span 以保持链路完整。
    - 确保 `Critical` 状态下全量熔断，优先保护系统存活。
- **server/tests/TraceSessionManager_unit_test.cpp**:
    - 追加 `PushReturnsAcceptedDeferredWhenSubmitFails`：模拟线程池过载，验证 Trace 自动退回内存队列并切换为 `ReadyRetryLater` 状态。
    - 追加 `BackpressureRecoversWhenWatermarkDropsBelowLow`：验证 0.55/0.75/0.90 三档水位的迟滞回线逻辑，确保恢复过程不发生状态抖动。

## Learning Tips
- **Newbie Tips**: 
    - 背压不只是简单的“拒绝”，要在内存积压（Span 计数）和下游处理能力（线程池队列）之间做双向闭环。
    - GTest 中使用 `#define private public` 虽然是“黑科技”，但在测试复杂状态机内部变量（如 `total_buffered_spans_`）时极其高效，能避免为了测试暴露过多的 Getter/Setter。
- **Function Explanation**:
    - `ShouldRejectIncomingTrace(bool trace_exists)`: 核心策略函数，封装了“新客拒、老客留”的降级逻辑。
    - `manager.Dispatch(key)`: 可以在单测中手动触发分发，从而人为压低水位验证恢复流程。
- **Pitfalls**:
    - **CMake 编译缓存坑**: 修改了单测文件但 `make` 可能没干活，必须确保二进制文件确实被重新 Link 才能跑出最新的 Case。
    - **参数数量匹配**: `TraceSessionManager` 的构造函数中间有个没默认值的 `token_limit` 参数，传参时必须补齐，否则会导致编译报错。

## Validation
- 成功运行 `test_trace_session_manager_unit`，24 个测试全部通过（含新增的 2 个背压深度测试）。
- 确认 Overload 状态下返回 `PushResult::RejectedOverload`，Submit 失败返回 `PushResult::AcceptedDeferred`。

---

fix(trace): 修正无线程池时误报 accepted 的静默丢数据问题

## Modification
- **server/core/TraceSessionManager.h**:
    - 为 `PushResult` 增加 `RejectedUnavailable`，显式表达“核心异步依赖缺失，Trace 管道当前不可用”。
- **server/core/TraceSessionManager.cpp**:
    - 在 `Push` 入口处增加 `thread_pool_ == nullptr` 的直接拒绝分支，避免 session 尚未真正进入任何执行路径时误报 `Accepted`。
    - 在 `Dispatch` 开头补充双保险防御，防止未来其他路径绕过 `Push` 直接调用 `Dispatch` 时再次把 trace 从内存摘掉后静默蒸发。
- **server/handlers/LogHandler.cpp**:
    - 将 `RejectedUnavailable` 映射为 `503`，响应体明确返回 `"Trace pipeline is unavailable"`，不再对客户端伪装成成功接收。
- **server/tests/TraceSessionManager_unit_test.cpp**:
    - 将错误语义的 `DispatchReturnsSafelyWhenThreadPoolIsNull` 改为 `PushRejectsWhenThreadPoolIsNull`，把测试关注点从“别崩”改成“别误报 accepted”。

## Learning Tips
- **Newbie Tips**:
    - 并发链路里最坑的不是 crash，而是“看起来成功、实际上丢数据”。这类静默错误如果被单测锁成绿色，后面会非常恶心。
    - `202 Accepted` 一旦对外发出去，语义就是“这条数据已经被服务端接住了”；如果后台根本没线程池，那就应该拒绝，而不是自欺欺人。
- **Function Explanation**:
    - `PushResult::RejectedUnavailable`: 用于区分“系统过载”与“关键依赖不存在”这两类本质不同的 503。
    - `LogHandler::handleTracePost(...)`: 现在会对 unavailable 和 overload 分开回包，便于前端或调用方排查。
- **Pitfalls**:
    - 只在 `Dispatch` 里做空线程池保护还不够，因为正常触发分发前 session 可能已经被摘掉；入口 `Push` 也必须先挡。
    - CTest 的历史日志文件会保留旧测试名，看到 `LastTest.log` 里的旧 case 别慌，那不代表新测试没生效。

## Validation
- 重新编译 `LogSentinel`、`test_trace_session_manager_unit`、`test_trace_session_manager_integration` 均通过。
- 重新执行 `ctest -R "^(TraceSessionManagerUnitTest|TraceSessionManagerIntegrationTest)\\." --output-on-failure`，35 个测试全部通过。
- 确认旧用例名 `DispatchReturnsSafelyWhenThreadPoolIsNull` 已替换为 `PushRejectsWhenThreadPoolIsNull`，当前行为与测试语义一致。

---

refactor(trace): 为 ready retry 会话增加失败计数与指数退避

## Modification
- **server/core/TraceSessionManager.h**:
    - 在 `TraceSession` 中新增 `retry_count` 与 `next_retry_tick`，把重试状态挂在会话本身，而不是散在时间轮逻辑里硬算。
    - 增加 `ComputeRetryDelayTicks(...)` 声明，统一收口退避 tick 计算。
- **server/core/TraceSessionManager.cpp**:
    - 在 `Push` 收到新 span 时清空 `retry_count/next_retry_tick`，让会话回到纯 collecting 语义。
    - 在 `Dispatch` 的 submit 失败回滚路径中递增 `retry_count`，并基于指数退避计算新的 `next_retry_tick`。
    - 将 `ScheduleRetryNode(...)` 改为优先使用 `session.next_retry_tick`，不再固定 `current_tick + 1`。
    - 在 `SweepExpiredSessions(...)` 中增加 `ReadyRetryLater` 的二次判断，确保未到 `next_retry_tick` 的会话不会提前重撞线程池。
- **server/tests/TraceSessionManager_unit_test.cpp**:
    - 扩展 `PushReturnsAcceptedDeferredAndRollsBackWhenSubmitFails`，补充首轮失败后 `retry_count=1`、`next_retry_tick=1` 的断言。
    - 新增 `RetryDelayBackoffGrowsOnRepeatedSubmitFailure`，验证第二次失败后会退避到更远的 tick，而不是继续每 tick 原地撞。

## Learning Tips
- **Newbie Tips**:
    - 收集态的控制和重试态的控制不是一回事。`Collecting` 关心的是“等不等后续 span”，`ReadyRetryLater` 关心的是“多久再试一次投递”。
    - 指数退避不一定要一步到位搞很复杂，先有 `retry_count + next_retry_tick` 这两个状态，后面才有资格继续调策略。
- **Function Explanation**:
    - `retry_count`: 连续 submit 失败次数，只在失败回滚时增加；新 span 到来时清零。
    - `next_retry_tick`: 下一次最早允许重试投递的 tick，`ScheduleRetryNode(...)` 只负责按这个时间挂槽。
    - `ComputeRetryDelayTicks(...)`: 当前采用最小指数退避，按 `1/2/4/8/16 tick` 增长并封顶。
- **Pitfalls**:
    - 如果 `ReadyRetryLater` 只靠 `LifecycleState` 不带具体时点，最终还是会退化成“每个 tick 都来撞一次”的假重试。
    - `ctest` 若在 build 完成前先跑，测试数量和用例名可能还是旧缓存；最终结果要以 build 完成后的二次回归为准。

## Validation
- 重新编译 `test_trace_session_manager_unit` 与 `test_trace_session_manager_integration` 通过。
- 重新执行 `ctest -R "^(TraceSessionManagerUnitTest|TraceSessionManagerIntegrationTest)\\." --output-on-failure`，36 个测试全部通过。
- 确认新用例 `TraceSessionManagerUnitTest.RetryDelayBackoffGrowsOnRepeatedSubmitFailure` 已进入 CTest 注册列表并执行通过。

---

test(trace): 补齐 retry backoff 的时点行为测试

## Modification
- **server/tests/TraceSessionManager_unit_test.cpp**:
    - 新增 `RetryBackoffDoesNotRetryBeforeNextRetryTick`，使用 `ThreadPool(1, 0)` 稳定制造 `submit` 失败。
    - 断言第一次失败后 `retry_count=1 / next_retry_tick=1`。
    - 断言 tick 走到 `1` 时允许第二次尝试，并把退避拉长到 `next_retry_tick=3`。
    - 断言 tick 走到 `2` 时不会提前重试，`retry_count` 保持不变。
    - 断言 tick 走到 `3` 时才发生第三次尝试，并继续把 `next_retry_tick` 推远。

## Learning Tips
- **Newbie Tips**:
    - 测退避别只测字段有没有变化，最值钱的是测“没到时间绝对不该重试”这种行为边界。
    - `max_queue_size=0` 比 `shutdown()` 更适合测 backoff，因为它表达的是“线程池对象还活着，但每次 submit 都失败”。
- **Function Explanation**:
    - `RetryBackoffDoesNotRetryBeforeNextRetryTick`: 锁住 `ReadyRetryLater` 会话的时间门禁语义，避免回归成“每 tick 原地撞一次”。
- **Pitfalls**:
    - 如果在 build 完成前先跑 `ctest`，新用例可能还没进入 CTest 注册列表；最终结果要以重新 link 后的二次回归为准。

## Validation
- 重新编译 `test_trace_session_manager_unit` 通过。
- 重新执行 `ctest -R "^TraceSessionManagerUnitTest\\.(RetryDelayBackoffGrowsOnRepeatedSubmitFailure|RetryBackoffDoesNotRetryBeforeNextRetryTick)$" --output-on-failure`，2 个 retry 相关用例全部通过。
- 确认新用例 `TraceSessionManagerUnitTest.RetryBackoffDoesNotRetryBeforeNextRetryTick` 已进入 CTest 注册列表。
