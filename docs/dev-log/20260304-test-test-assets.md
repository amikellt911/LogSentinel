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
