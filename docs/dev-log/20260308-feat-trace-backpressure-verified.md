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
