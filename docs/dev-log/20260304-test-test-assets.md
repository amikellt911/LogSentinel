# 20260304-test-test-assets

Git Commit Message:
test(tests): 软下线部分历史测试并更新测试台账

Modification:
- server/CMakeLists.txt
- server/tests/legacy/SqliteConfigRepository_test.cpp
- server/tests/legacy/SqliteLogRepository_test.cpp
- server/tests/legacy/util_traceidgenerate_test.cpp
- server/tests/legacy/LogBatcher_test.cpp
- server/tests/legacy/README.md
- docs/TEST_ASSET_LEDGER.md
- docs/todo-list/Todo_TestAssets.md

Learning Tips:
Newbie Tips:
- 软下线推荐“先迁移到 legacy + 退出默认 CTest/CI”，不要直接删除文件。这样回滚和对照都方便。
- CTest 的测试来源通常来自 `gtest_discover_tests`，只把文件移目录但不改 CMake，会导致构建报路径错误。

Function Explanation:
- `gtest_discover_tests(target)`：在 CMake 配置/构建阶段自动从 gtest 二进制中发现用例并注册给 CTest。
- `git mv`：在移动文件时保留 Git 历史关系，后续追踪 blame 更友好。

Pitfalls:
- 同仓库并发执行多个 `git` 命令可能触发 `.git/index.lock` 冲突；出现时先确认没有悬挂进程，再重试失败命令。
- 台账如果不和 CMake 同步，会出现“文档说下线了但 CI 还在跑”的认知不一致问题。

---

追加记录（HttpContext 测试修复）:

Git Commit Message:
fix(http): 修复请求头大小写导致 HttpContext 测试异常

Modification:
- server/http/HttpRequest.h

Learning Tips:
Newbie Tips:
- HTTP Header 名大小写不敏感，推荐在“写入和读取”两端都做同一套规范化（例如统一转小写）。
- `unordered_map::at` 适合“必须存在”的场景；对于外部输入（如 HTTP 头）更适合先 `find` 再处理缺失分支。

Function Explanation:
- `std::transform`：对字符串逐字符转换，本次用于把查询 key 转为小写。
- `std::tolower`：字符转小写，配合 `unsigned char` 转换可避免未定义行为风险。

Pitfalls:
- 只在解析时把 key 转小写，但读取时不转，会出现“存得进、取不到”的隐藏一致性问题。

---

追加记录（Unit CI 覆盖扩展）:

Git Commit Message:
test(ci): 扩展 unit 工作流覆盖核心单测套件

Modification:
- .github/workflows/unit.yml
- docs/todo-list/Todo_TestAssets.md

Learning Tips:
Newbie Tips:
- `ctest -R` 可以用正则一次选择多组测试，适合把“快且稳定”的套件收敛到 unit 阶段。
- unit 阶段优先放无外部依赖测试，减少 CI 抖动和偶发失败。

Function Explanation:
- `ctest -R "<regex>"`：只运行测试名匹配正则的用例。
- `|`：正则里的“或”，本次用于并行选择多个 Test Suite 前缀。

Pitfalls:
- 正则如果没加起止锚点，可能误匹配到不想跑的用例；本次使用 `^(A|B|C)\\.` 精确匹配测试套件前缀。

---

追加记录（Trace AI provider 选择与 Gemini trace 补齐）:

Git Commit Message:
feat(ai): 增加 trace ai provider 选择并补齐 gemini trace 分析

Modification:
- server/ai/TraceAiBackend.h
- server/ai/TraceProxyAi.h
- server/ai/TraceProxyAi.cpp
- server/ai/TraceAiFactory.h
- server/ai/TraceAiFactory.cpp
- server/src/main.cpp
- server/CMakeLists.txt
- server/ai/proxy/providers/gemini.py
- docs/todo-list/Todo_TraceAiProvider.md

Learning Tips:
Newbie Tips:
- 当多个后端共享同一协议时，优先做“统一执行实现 + backend 参数”，比每个后端单独写一份 C++ 类更省维护成本。
- 工厂不是必须，但能把“创建策略”从 `main.cpp` 抽离，后续加 provider 时改动范围更小。

Function Explanation:
- `TryParseTraceAiBackend(...)`：把启动参数字符串解析为后端枚举，统一做入参校验。
- `CreateTraceAiProvider(...)`：根据选项创建 `TraceAiProvider` 实例，当前返回统一的 `TraceProxyAi`。

Pitfalls:
- Python 侧如果 `GeminiProvider.analyze_trace` 未实现，即使 C++ 侧能切到 `gemini`，运行时仍会返回 500。
- 仅靠 `--trace-ai-provider gemini` 不能自动保证代理就绪；代理进程仍需手动启动或配合 `--auto-start-proxy`。

---

追加记录（Proxy 默认 Prompt 解耦）:

Git Commit Message:
refactor(ai-proxy): 拆分日志与trace默认prompt并收敛到schemas

Modification:
- server/ai/proxy/main.py
- server/ai/proxy/schemas.py
- docs/todo-list/Todo_TraceAiProvider.md

Learning Tips:
Newbie Tips:
- 不同输入语义（单日志 vs Trace 树）应使用不同默认 Prompt，否则模型会按错误上下文分析，结果稳定性会明显下降。
- Prompt 模板集中放在 `schemas.py` 这种“协议层”位置，比散落在路由函数里更利于维护。

Function Explanation:
- `provider.analyze(...)`：用于单日志语义分析。
- `provider.analyze_trace(...)`：用于 trace 聚合语义分析。

Pitfalls:
- `/analyze/{provider}` 如果误调到 `analyze_trace`，会让单日志接口语义漂移且调试难度上升。

---

追加记录（新增 Gemini 端到端冒烟模式）:

Git Commit Message:
test(smoke): 新增 gemini-e2e 模式验证 trace 真分析全链路

Modification:
- server/tests/smoke_trace_spans.py
- docs/todo-list/Todo_TraceAiProvider.md

Learning Tips:
Newbie Tips:
- 真模型测试应与 mock 冒烟分层，避免把网络波动误判成主链路回归失败。
- 冒烟脚本优先复用现有入口（通过 `--mode` 扩展），比新建多个脚本更容易维护。

Function Explanation:
- `wait_proxy_ready(...)`：在 gemini-e2e 启动前先探测 proxy/provider 可用性，减少无效后端启动时间。
- `probe_proxy_and_webhook(...)`：失败时输出外部依赖探针信息，缩短排障路径。

Pitfalls:
- gemini-e2e 依赖真实 API Key 与网络环境，默认不应纳入 CI 强制门禁。
- 真模型 `risk_level` 存在波动，不应在 e2e 中做“固定值等值断言”，应改为枚举合法性与字段完整性断言。

---

追加记录（修复 Gemini E2E 误报基础落库超时）:

Git Commit Message:
fix(test): gemini-e2e 按 ai 超时自动放宽等待窗口

Modification:
- server/tests/smoke_trace_spans.py
- docs/todo-list/Todo_TraceAiProvider.md

Learning Tips:
Newbie Tips:
- 当链路是“先调 AI 再落库”时，数据库等待时间必须至少覆盖 AI 超时窗口，否则会出现“实际在处理，但测试提前判失败”。
- 真模型测试默认要用比 mock 更长的超时预算，否则稳定性会显著下降。

Function Explanation:
- `max(a, b)`：用于给超时设置保底值，避免配置过小导致误判。
- `trace_ai_timeout_ms / 1000.0`：把毫秒参数转换为秒，用于与 `db-timeout`、`analysis-timeout` 对齐比较。

Pitfalls:
- 仅增加 `--trace-ai-timeout-ms` 不够；如果 `db-timeout` 仍是 10 秒，测试会在 AI 返回前失败。

---

追加记录（Trace AI 异常兜底不阻断主链路）:

Git Commit Message:
fix(core): trace ai异常时降级仅落基础链路避免任务中断

Modification:
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceAiProvider.md

Learning Tips:
Newbie Tips:
- 超时参数只是“等待上限”，不能保证外部调用一定成功；主业务链路仍需在调用点做 `try/catch` 兜底。
- 把异常兜底放在调度层（TraceSessionManager）可以保护线程池任务不被未捕获异常打断。

Function Explanation:
- `try/catch`：用于捕获 AI 调用中的网络异常、协议解析异常，保证后续 `SaveSingleTraceAtomic` 仍执行。
- `SaveSingleTraceAtomic(..., analysis_ptr, ...)`：当 `analysis_ptr == nullptr` 时只写 summary/span，保持单次事务写入。

Pitfalls:
- 仅在 `TraceProxyAi` 设置超时而不在上层捕获异常，仍会导致任务异常退出，出现“基础数据未落库”的假象。

---

追加记录（AI 异常失败态分析落库）:

Git Commit Message:
feat(core): ai异常时写入失败态trace_analysis便于后续重试

Modification:
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceAiProvider.md

Learning Tips:
Newbie Tips:
- “降级不丢状态”比“静默失败”更重要：即使 AI 失败，也应写一条可识别的失败态记录，方便前端提示和后续重试。
- 失败态记录建议字段固定（例如 `summary=AI_ANALYSIS_FAILED`），便于查询和统计，不要每次拼不同文案。

Function Explanation:
- `catch (const std::exception& e)`：捕获标准异常并拿到 `e.what()` 作为失败根因。
- `SaveSingleTraceAtomic(..., analysis_ptr, ...)`：当 `analysis_ptr` 指向失败态记录时，summary/span/analysis 会在同一事务中一次性落库。

Pitfalls:
- 如果 catch 后直接 `analysis_ptr = nullptr`，虽然主链路可用，但业务侧无法区分“未分析”还是“分析失败”。

---

追加记录（Trace 会话超时分发定时器）:

Git Commit Message:
feat(core): 增加trace会话超时巡检并触发自动分发

Modification:
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- server/src/main.cpp
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:
Newbie Tips:
- 对于“可能永远收不到结束标记”的流式输入，必须有超时兜底，否则数据会一直卡在内存中看不到结果。
- 现阶段优先用固定周期巡检（`runEvery`）比直接上时间轮更稳，先保证功能正确再考虑复杂优化。

Function Explanation:
- `EventLoop::runEvery(interval, cb)`：按固定周期执行回调；本次用于定时触发 `SweepExpiredSessions`。
- `SweepExpiredSessions(now_ms, idle_timeout_ms, max_dispatch_per_tick)`：扫描超时未更新的 trace 会话并调用 `Dispatch`。

Pitfalls:
- 直接在遍历 `sessions_` 时调用 `Dispatch` 会改动容器导致迭代失效；应先收集 `trace_key`，再批量分发。
