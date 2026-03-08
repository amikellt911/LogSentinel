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
