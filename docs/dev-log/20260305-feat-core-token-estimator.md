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

---

追加记录（token_limit 集成测试补齐）:

Git Commit Message:
test(integration): 增加token阈值触发分发集成用例

Modification:
- server/tests/TraceSessionManager_integration_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:
Newbie Tips:
- token_limit 行为除了单测，还要补“真实线程池 + 真实 SQLite”的集成验证，才能确认触发链路没有被异步流程吞掉。
- 集成测试中 token_limit 建议由 `TokenEstimator` 动态计算，不要写死常量，避免估算口径调整后测试大面积失效。

Function Explanation:
- `TokenEstimator estimator; estimator.Estimate(span)`：在测试里复用生产口径得到阈值，确保行为断言和线上一致。
- `WaitForCount("SELECT COUNT(*) FROM trace_summary;", 1, timeout)`：轮询等待异步落库完成，避免固定 sleep 带来的抖动。

Pitfalls:
- 如果 `capacity` 太小，会先走容量分发路径，导致用例无法证明 token_limit 触发语义。
- 如果接入 AI provider，测试会引入外部依赖抖动；本用例故意用 `trace_ai=nullptr` 聚焦 token 行为。

---

追加记录（ConfigRepository 高优先级事务与错误处理修复）:

Git Commit Message:
fix(persistence): 修复配置写入事务边界与提交错误检查

Modification:
- server/persistence/SqliteConfigRepository.cpp
- docs/todo-list/Todo_ConfigRepository.md

Learning Tips:
Newbie Tips:
- 事务一旦 `BEGIN` 成功，后续任何阶段（包括 `prepare`）失败都必须进入 `ROLLBACK` 路径，否则连接事务状态可能脏掉。
- `sqlite3_step` 的返回码必须就地保存并检查，不能复用旧的 `rc` 变量，否则会把真实错误吞掉。

Function Explanation:
- `checkSqliteError(db, rc, context)`：统一把 SQLite 返回码转换为带上下文的异常，减少静默失败。
- `sqlite3_clear_bindings(stmt)`：在复用 prepared statement 时清空上次绑定值，避免参数残留污染下一轮执行。

Pitfalls:
- `COMMIT` 不检查返回码会导致“看起来成功、实际未提交”的假成功状态，后续排障很难定位。
- 在 `try` 外 `prepare` 并直接 `throw`，会绕过统一 `catch+ROLLBACK`，这是配置偶发锁表问题的常见来源。

---

追加记录（配置枚举值标准化）:

Git Commit Message:
refactor(config): 统一ai provider与language的存储值和前端取值

Modification:
- server/persistence/ConfigTypes.h
- server/persistence/SqliteConfigRepository.cpp
- client/src/stores/system.ts
- client/src/views/Settings.vue
- client/src/layout/MainLayout.vue
- docs/todo-list/Todo_ConfigRepository.md

Learning Tips:
Newbie Tips:
- 配置项的“存储值”和“展示文案”最好分开：数据库/API 存稳定机器值，前端下拉框负责展示友好文案。
- 这种枚举标准化第一版直接用硬编码映射就够了，不需要为了几项枚举上复杂配置系统。

Function Explanation:
- `NormalizeAiProviderValue(...)`：把 `Local-Mock/Gemini/OpenAI/Azure` 等历史或展示值统一收敛为 `mock/gemini/openai/azure`。
- `NormalizeAiLanguageValue(...)`：把 `English/中文/en/zh` 统一收敛为 `en/zh`。

Pitfalls:
- 如果只改后端不改前端，UI 会继续把展示值当真值使用，联调时会出现“保存后又变回来”的错觉。
- 这次前端 `npm run build` 失败是仓库中既有的 TypeScript 问题，不是本次 provider/language 标准化引入的新错误。
