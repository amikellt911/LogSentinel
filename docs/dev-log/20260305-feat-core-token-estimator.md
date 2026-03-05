# 20260305-feat-core-token-estimator

Git Commit Message:
feat(core): 实现TokenEstimator最小可用估算并接入token阈值分发测试

Modification:
- server/core/TokenEstimator.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:
Newbie Tips:
- `std::string::size()` 是 O(1)，真正的成本通常在 JSON 解析和对象遍历，不在取长度本身。
- token 估算第一版不必追求“模型同款精度”，先保证口径稳定和可测试，比盲目追精度更重要。

Function Explanation:
- `TokenEstimator::Estimate(const SpanEvent&)`：按 span 的文本字段长度、数字位数和固定 JSON 结构开销估算 token。
- `std::max<size_t>(tokens, 1)`：保证每条 span 至少贡献 1 个 token，避免空输入让阈值永远不触发。

Pitfalls:
- 单测如果硬编码具体 token 数值，后续调口径会导致大量无意义回归；更稳的是断言“阈值触发行为”是否正确。
- token_limit 用例如果通过手工改内部状态（例如直接改 `token_count`）会掩盖真实估算链路问题，容易出现线上和测试口径不一致。
