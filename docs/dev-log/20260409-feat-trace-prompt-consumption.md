# Git Commit Message

feat(settings): 接通 trace ai 的语言与业务 prompt 冷启动消费

# Modification

- `server/ai/TracePromptRenderer.h`
- `server/ai/TracePromptRenderer.cpp`
- `server/ai/TraceAiFactory.h`
- `server/ai/TraceAiFactory.cpp`
- `server/ai/TraceProxyAi.h`
- `server/ai/TraceProxyAi.cpp`
- `server/ai/proxy/main.py`
- `server/ai/proxy/schemas.py`
- `server/ai/proxy/providers/gemini.py`
- `server/src/main.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

# 这次补了哪些注释

- 在 `server/ai/TracePromptRenderer.h` 和 `server/ai/TracePromptRenderer.cpp` 里补了中文注释，说明为什么结构化业务 prompt 先在 C++ 冷启动边界解析和渲染，以及为什么最终返回的是带 `{{TRACE_CONTEXT}}` 占位符的模板而不是直接带 trace 内容的字符串。
- 在 `server/ai/TraceAiFactory.h` 和 `server/ai/TraceAiFactory.cpp` 里补了中文注释，说明为什么最终 trace prompt 要跟 `backend/base_url/timeout` 一起进入 `TraceAiProvider` 构造路径。
- 在 `server/ai/TraceProxyAi.h` 和 `server/ai/TraceProxyAi.cpp` 里补了中文注释，说明为什么 trace 请求体从 `text/plain` 改成 JSON，以及 `prompt_template_` 缓存的到底是什么。
- 在 `server/ai/proxy/main.py` 里补了中文注释，说明为什么 trace 路由优先吃 C++ 下发的 prompt 模板，并在最后一步才注入本次 `trace_text`。
- 在 `server/ai/proxy/schemas.py` 里补了中文注释，说明为什么默认 `TRACE_PROMPT_TEMPLATE` 现在只作为兜底模板存在。
- 在 `server/ai/proxy/providers/gemini.py` 里补了中文注释，说明为什么 trace provider 不再把 `trace_text` 再追加一次，避免重复上下文。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 `ai_language + active prompt` 本轮按冷启动收口，并在启动时一次性渲染最终 trace prompt 模板。
- 在 `server/CMakeLists.txt` 里补了中文注释，说明为什么新增 `TracePromptRenderer.cpp` 到 `ai_module`。

# Learning Tips

## Newbie Tips

Prompt 分层这件事，不等于“最后不能拼字符串”。真正要避免的，是把“固定系统规则”和“业务偏好”都放在一个自由文本框里乱写。工程上更稳的做法是：先把业务 prompt 结构化保存，再在一个明确边界里把它渲染成业务 guidance，最后和固定系统 prompt 组装。

## Function Explanation

`TracePromptRenderer` 这次扮演的是“冷启动边界渲染器”。它接收 Settings 存下来的 `active_prompt.content` 和 `ai_language`，负责两件事：一是兼容“旧自由文本”和“新结构化 JSON”两种 Prompt 内容；二是把它们统一渲染成 Trace AI 最终可用的 Prompt 模板。这样 `main.cpp` 不需要自己手搓字符串，`TraceProxyAi` 也不需要懂 Prompt 结构。

## Pitfalls

不要让模型输入里的 trace 上下文被重复拼两次。之前如果 proxy route 先把 `trace_text` 拼进 Prompt，provider 又自己再 `prompt + trace_text` 一次，就会让同一份 Trace 内容在模型上下文里出现两遍，既浪费 token，也会让模型对“哪个上下文才是主数据”产生歧义。

# Verification

- `python3 -m py_compile server/ai/proxy/main.py server/ai/proxy/schemas.py server/ai/proxy/providers/gemini.py server/ai/proxy/providers/mock.py`
- `cmake --build server/build --target LogSentinel`

# 追加记录：2026-04-09 trace AI 接入 provider / model / api_key 冷启动消费

## Git Commit Message

feat(settings): 接通 trace ai 的 provider model key 冷启动配置

## Modification

- `server/ai/TraceAiFactory.h`
- `server/ai/TraceAiFactory.cpp`
- `server/ai/TraceProxyAi.h`
- `server/ai/TraceProxyAi.cpp`
- `server/ai/proxy/schemas.py`
- `server/ai/proxy/main.py`
- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/ai/TraceAiFactory.h` 和 `server/ai/TraceAiFactory.cpp` 里补了中文注释，说明为什么 `provider/model/api_key/prompt` 要作为同一组 Trace AI 冷启动参数一起下发。
- 在 `server/ai/TraceProxyAi.h` 和 `server/ai/TraceProxyAi.cpp` 里补了中文注释，说明为什么 `TraceProxyAi` 要缓存 `model/api_key`，以及为什么请求体里只在值非空时才透传。
- 在 `server/ai/proxy/main.py` 和 `server/ai/proxy/schemas.py` 里补了中文注释，说明为什么 trace route 继续保持“C++ 传什么，proxy 就透什么”的薄层语义。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 `trace AI` 的 `provider/model/api_key` 也一起改成冷启动决策，并采用 `CLI > Settings > 默认值`。

## Learning Tips

### Newbie Tips

同一条调用链上的配置不要分裂消费。既然 `trace prompt` 已经走冷启动，那么 `trace AI` 的 `provider/model/api_key` 也应该走同一套启动期决策。否则你会得到一种很脏的状态：语言和业务 prompt 来自 Settings，新模型和新密钥却还停在另一条旧链路里。

### Function Explanation

这次 `TraceAiFactoryOptions` 新增的 `model` 和 `api_key`，本质上不是在“扩展工厂能力”，而是在把 Trace AI 的完整请求上下文集中起来。工厂本身还是只负责创建 provider，只是 provider 构造时拿到的冷启动参数从 3 个变成了 5 个。

### Pitfalls

不要把 `api_key` 这种敏感字段直接原样打到启动日志里。这次日志里只输出 `<configured>` 或 `<empty>`，否则一旦演示时开着终端或把日志收进文件，密钥就会直接泄露。

## Verification

- `python3 -m py_compile server/ai/proxy/main.py server/ai/proxy/schemas.py server/ai/proxy/providers/gemini.py server/ai/proxy/providers/mock.py`
- `cmake --build server/build --target LogSentinel`

# 追加记录：2026-04-09 收口 main.cpp 主路由旧链挂载

## Git Commit Message

refactor(router): 从主链路移除旧 history 和结果查询挂载

## Modification

- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/src/main.cpp` 的主路由注册段前补了中文注释，说明为什么这一步只保留“新 Trace 读写 + 运行态快照 + Settings”三类入口，以及为什么旧 `/logs`、`/results/*`、`/history*` 不再继续挂主路由。
- 在 `server/src/main.cpp` 的 `LogHandler` 构造位置补了中文注释，说明为什么旧接口虽然还留在 `LogHandler` 文件里，但主路由已经不再注册，只暂时复用同一个 handler 承接 `/logs/spans`。

## Learning Tips

### Newbie Tips

主链路收口时，先撤“入口挂载”通常比直接删整套实现更稳。因为入口才决定外部流量能不能打进来，先把旧入口拔掉，就已经能避免前端、脚本和答辩流程继续误走废弃链路；真正删文件和删依赖可以留到下一刀单独做。

### Function Explanation

`Router::add()` 这一层就是 HTTP 入口到业务 handler 的绑定表。你从这里把某个 path/method 对拿掉，本质上就是让这条链路从“可访问接口”变成“仓库里还留着实现代码，但外面已经打不进来”。

### Pitfalls

不要一边说“旧链废弃了”，一边还把旧路由继续挂在 `main.cpp`。这样最坏的结果不是代码脏，而是演示和联调时你自己都会搞不清到底哪条链是真入口，最后查问题会在两套链路之间来回误判。

## Verification

- `cmake --build server/build --target LogSentinel`

# 追加记录：2026-04-09 脱钩 LogHandler 旧依赖并清理 main.cpp 残留构造

## Git Commit Message

refactor(handler): 让旧日志链依赖可空并从主程序移除构造

## Modification

- `server/handlers/LogHandler.h`
- `server/handlers/LogHandler.cpp`
- `server/src/main.cpp`
- `server/tests/LogHandler_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/handlers/LogHandler.h` 里补了中文注释，说明为什么 `repo/batcher` 现在只剩旧链兼容用途，以及为什么允许为空。
- 在 `server/handlers/LogHandler.cpp` 的 `handlePost()` 和 `handleGetResult()` 里补了中文注释，说明为什么旧依赖缺失时要显式返回 503，而不是继续空指针崩溃。
- 在 `server/src/main.cpp` 的 `LogHandler` 构造位置补了中文注释，说明为什么主程序现在明确传空旧依赖，只保留 `/logs/spans` 新链。
- 在 `server/tests/LogHandler_test.cpp` 里补了中文注释，说明新增测试锁定的是“旧接口在主链脱钩后必须 fail closed”的语义。

## Learning Tips

### Newbie Tips

主程序“已经不用某条链路”不等于代码里“已经没有那条链路的构造依赖”。真正的脱钩要看两件事：一是入口是不是已经摘掉，二是构造路径是不是还能在完全不创建旧对象的前提下正常启动。两件都做到，才算真的从主链上拿掉。

### Function Explanation

这次 `LogHandler` 没有直接删除旧 `handlePost/handleGetResult`，而是改成 fail-closed。意思是：接口代码还留着，但依赖为空时立刻返回 503，明确告诉调用方“这条旧链现在不可用”。这比保留空指针隐患或者悄悄返回假成功都稳。

### Pitfalls

不要在同一个 build 目录里并发跑两个 `cmake --build`。这次就撞到了 `ranlib: file truncated`，本质是两个 ninja 任务同时改同一个静态库产物，结果把中间产物打坏了。构建验证最好串行跑。

## Verification

- `cmake --build server/build --target LogSentinel`
- `cmake --build server/build --target test_log_handler`
- `./server/build/test_log_handler`

# 追加记录：2026-04-09 彻底移除旧日志分析链活代码

## Git Commit Message

refactor(core): 移除旧日志分析链活代码与构建入口

## Modification

- `server/handlers/LogHandler.h`
- `server/handlers/LogHandler.cpp`
- `server/src/main.cpp`
- `server/CMakeLists.txt`
- `server/tests/LogHandler_test.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/ai/MockTraceAi.cpp`
- `server/ai/proxy/main.py`
- `server/http/README.md`
- `server/threadpool/README.md`
- `server/persistence/README.md`
- `docs/PROMPT_LAYERING_NOTES.md`
- `docs/TRACE_WRK_BENCHMARK_GUIDE.md`
- `AGENTS.md`
- `docs/todo-list/Todo_Settings_MVP5.md`
- 删除：
  - `server/core/AnalysisTask.cpp`
  - `server/core/AnalysisTask.h`
  - `server/core/LogBatcher.cpp`
  - `server/core/LogBatcher.h`
  - `server/persistence/SqliteLogRepository.cpp`
  - `server/persistence/SqliteLogRepository.h`
  - `server/ai/AiProvider.h`
  - `server/ai/GeminiApiAi.cpp`
  - `server/ai/GeminiApiAi.h`
  - `server/ai/MockAI.cpp`
  - `server/ai/MockAI.h`
  - `server/handlers/HistoryHandler.cpp`
  - `server/handlers/HistoryHandler.h`
  - `server/src/testServerPoolSqlite.cpp`
  - `server/src/testWithDashboard.cpp`
  - `server/src/testWithRouter.cpp`
  - `server/src/testgemini.cpp`
  - `server/src/testserverPoolSqliteMock.cpp`
  - `server/src/testserverpool.cpp`
  - `server/tests/wrk/mvp1_performance_test.sh`
  - `server/tests/wrk/performance_test.sh`
  - `server/tests/wrk/performance_test2.sh`

## 这次补了哪些注释

- 在 `server/handlers/LogHandler.h` 里补了中文注释，说明 `LogHandler` 现在只承接 `/logs/spans`，旧 `/logs` 和 `/results/*` 已经从主链构造和路由一起收口，不再继续拖着旧依赖类型。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，说明为什么要把 `sealed_grace_window_ms`、`retry_base_delay_ms` 和后面的水位阈值参数显式写出来，避免长参数列表继续错位。
- 在 `server/ai/MockTraceAi.cpp` 与 `server/ai/proxy/main.py` 里补了中文注释，说明当前主链 AI 走的是 Trace proxy 统一协议，不再经过旧 `MockAI/GeminiApiAi` 那条实现链。
- 在 `server/src/main.cpp` 里保留并补强了中文注释，说明当前主程序只挂 Trace 新链、运行态快照和 Settings 入口，旧 history/result 路由不再注册。

## Learning Tips

### Newbie Tips

删旧链不能只看“源码文件删掉了没有”。真正要确认的是四件事一起成立：主路由不再挂入口、主程序不再构造旧对象、CMake 不再编旧目标、活跃脚本不再打旧接口。少一项都不算真收口。

### Function Explanation

当前主链 AI 的句柄已经不是旧 `AiProvider` 了，而是 `TraceAiProvider` 这层抽象；主程序启动时通过 `TraceAiFactory` 创建具体实现，当前活跃实现是 `TraceProxyAi`，它把请求统一发给 Python proxy。

### Pitfalls

长参数列表一旦中间插入了新配置项，最容易出现的不是编译不过，而是“还能编，但语义串位”。这次 `TraceSessionManager` 单测就有这个坑，所以凡是要传到尾部 `system_runtime_accumulator` 这种位置的调用，必须把中间新增参数显式补齐。

## Verification

- `cmake -S server -B server/build`
- `cmake --build server/build --target LogSentinel`
- `cmake --build server/build --target LogSentinel test_log_handler test_trace_session_manager_unit`
- `./server/build/test_log_handler`
- `./server/build/test_trace_session_manager_unit`

## Verification Result

- `LogSentinel`：通过
- `test_log_handler`：5/5 通过
- `test_trace_session_manager_unit`：36 条里 27 条通过，剩余 9 条失败；失败点集中在 sealed deadline、背压阈值和系统运行态断言，属于旧测试口径未跟上当前实现，不是这次删旧链引入的新编译错误

# 追加记录：2026-04-09 为 AI 降级配置补 fallback api key

## Git Commit Message

feat(settings): 为 ai 降级配置补 fallback api key

## Modification

- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/persistence/ConfigTypes.h` 里补了中文注释，说明为什么自动降级不能默认复用主模型的 `api_key`，而是要把 `provider/model/api_key` 当成完整备用三元组保存。
- 在 `server/persistence/SqliteConfigRepository.cpp` 里补了中文注释，说明为什么仓储层要单独映射 `ai_fallback_api_key`，以及继续偷懒复用主 key 会导致什么失败模式。
- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么设置页要把 `Fallback API Key` 和 `Fallback Provider/Model` 放在一起展示、一起回填、一起保存。

## Learning Tips

### Newbie Tips

自动降级不是“换个模型名”这么简单。既然允许切到另一个 provider，那么凭证也必须能一起切。否则表面上开了降级，真正失败时还是会因为拿着主 provider 的 key 去打备用 provider 而继续报错。

### Function Explanation

这次新增的 `ai_fallback_api_key` 只是配置面字段，不代表降级执行链已经写完。它现在解决的是“配置数据得先存得住、取得回、页面能改”，这样后面做熔断/降级状态机时，读取到的才是一套完整备用配置。

### Pitfalls

不要把“主 key 为空时自动退回 fallback key”或者“fallback key 为空时自动复用主 key”这种偷懒逻辑提前塞进存储层。存储层应该只忠实保存用户表达的配置；真正要不要兜底、在哪条分支兜底，应该留给上层降级策略决定。

## Verification

- `cmake --build server/build --target LogSentinel`
- `npm run build`

## Verification Result

- `LogSentinel`：通过
- `npm run build`：未通过，但当前失败项均来自仓库既有前端 TS 问题；本次改动涉及的 `SettingsPrototype.vue` 不再新增自己的构建错误

# 追加记录：2026-04-09 修正 TraceSessionManager 旧单测口径

## Git Commit Message

test(core): 修正 TraceSessionManager 旧单测口径

## Modification

- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，说明为什么“单 tick 封口”用例要显式把 `sealed_grace_window_ms` 设成 `500ms`，不能继续误吃当前默认的 `1000ms`。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，说明为什么 `SystemRuntimeAccumulator` 现在必须经过 `OnTick()` 才会把累计值发布成快照，测试不能再沿用“写完即读到 snapshot”的旧心智。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，说明为什么背压测试要显式补齐 `sealed/retry` 这两个新参数位，避免后面的 `wheel_size/hard_limit` 串位。

## Learning Tips

### Newbie Tips

长参数列表最危险的地方不是“编译不过”，而是“还能编，但语义已经错位”。一旦中间新增了两个参数，后面的硬限制、阈值、依赖注入全都可能悄悄传错位置，这种 bug 比直接编译失败更难发现。

### Function Explanation

`sealed_grace_window_ms` 会先经过 `ComputeDelayTicks()` 按 `wheel_tick_ms` 向上取整，所以 `1000ms / 500ms` 对应的是 `2 tick`，不是 `1 tick`。测试如果要锁死“单 tick 封口”，就必须把这个配置显式设成 `500ms`。

### Pitfalls

`SystemRuntimeAccumulator` 现在走的是“热路径只做原子累计 + OnTick 发布成品快照”的路线。这样请求线程便宜了，但测试如果还直接拿 `BuildSnapshot()` 等待最新值，就会一直读到旧发布结果，看起来像没记账，实际上只是没采样发布。

## Verification

- `cmake --build server/build --target test_trace_session_manager_unit`
- `./server/build/test_trace_session_manager_unit --gtest_filter='TraceSessionManagerUnitTest.PushSealsWhenCapacityReachedAndDispatchesAfterOneTick:TraceSessionManagerUnitTest.PushSealsWhenTokenLimitReachedAndDispatchesAfterOneTick:TraceSessionManagerUnitTest.PushSealsWhenDuplicateSpanIdAppearsAndDispatchesAfterOneTick:TraceSessionManagerUnitTest.SystemRuntimeAccumulatorTracksAiLifecycleAndUsageAfterAiCompletion:TraceSessionManagerUnitTest.RefreshOverloadStatePublishesSystemBackpressureStatus:TraceSessionManagerUnitTest.PushRejectsNewTraceButAllowsExistingTraceWhenOverload:TraceSessionManagerUnitTest.PushRejectsExistingTraceWhenCritical:TraceSessionManagerUnitTest.PushReturnsAcceptedDeferredWhenSubmitFails:TraceSessionManagerUnitTest.BackpressureRecoversWhenWatermarkDropsBelowLow'`
- `./server/build/test_trace_session_manager_unit`

## Verification Result

- 目标 9 条失败用例：9/9 通过
- `test_trace_session_manager_unit`：36/36 通过

# 追加记录：2026-04-09 移除 Trace 主链的废弃调试附属表支线

## Git Commit Message

refactor(persistence): 移除 trace 废弃调试附属表支线

## Modification

- `server/ai/TraceAiProvider.h`
- `server/persistence/TraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/tests/TraceSessionManager_integration_test.cpp`
- `server/tests/LogHandler_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/persistence/SqliteTraceRepository.cpp` 里补了中文注释，说明为什么 `SaveAnalysisBatch()` 仍然要把 `risk_level` 回写到 `trace_summary`，以及为什么 `SaveSingleTraceAtomic()` 现在只保留 `summary / spans / analysis` 三段事务写入。
- 在 `server/tests/LogHandler_test.cpp` 里补了中文注释，说明这个 fake repo 为什么只保留当前主链真实还会经过的最小接口集合，避免测试继续绑在废弃支线上。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，说明 fake repo 只记录当前主链真实落库的三段数据。
- 在 `server/tests/TraceSessionManager_integration_test.cpp` 里补了中文注释，说明为什么保护性封口测试也必须推进 2 tick，而不是继续沿用旧的 1 tick 口径。
- 在 `server/tests/SqliteTraceRepository_test.cpp` 里补了中文注释，说明 `SaveSingleTraceAtomicWithAnalysis` 就是当前三段原子写的完整路径，不再夹带废弃附属表。

## Learning Tips

### Newbie Tips

删一条废弃支线，不是把表删掉就完了。真正要收口的是一整条数据流：类型定义、仓储抽象、缓冲层、SQLite 实现、fake repo、单测入口都得一起断掉。只删中间一层，编译期或测试期迟早会把你拽回来。

### Function Explanation

`SaveAnalysisBatch()` 现在的职责很单纯：批量写 `trace_analysis`，然后把最终风险等级回写到 `trace_summary`。也就是说，它处理的是“分析结果落库”和“列表页读优化”这两件强相关的事，不再夹带别的调试数据。

### Pitfalls

接口裁剪最容易漏的是测试里的 fake/stub。活代码签名改了，如果 fake repo 还保留旧虚函数，看起来像只是测试文件没跟上，实际上是在说明“你删的不是一条完整支线，只是把生产代码切断了一半”。

## Verification

- `cmake --build server/build --target test_sqlite_trace_repo`
- `cmake --build server/build --target test_trace_session_manager_unit`
- `cmake --build server/build --target test_log_handler`
- `cmake --build server/build --target test_trace_session_manager_integration`
- `cmake --build server/build --target LogSentinel`
- `./server/build/test_sqlite_trace_repo`
- `./server/build/test_trace_session_manager_unit`
- `./server/build/test_log_handler`
- `./server/build/test_trace_session_manager_integration`

## Verification Result

- `test_sqlite_trace_repo`：12/12 通过
- `test_trace_session_manager_unit`：36/36 通过
- `test_log_handler`：5/5 通过
- `test_trace_session_manager_integration`：13/13 通过
- `LogSentinel`：编译通过
- 补充说明：integration 里 `capacity/token_limit/duplicate_span` 相关用例已经同步到当前统一 `sealed_grace_ticks_` 语义，不再保留旧的“保护性封口只推进 1 tick”测试口径。

# 追加记录：2026-04-09 接通 trace retention 冷启动清理

## Git Commit Message

feat(retention): 接通 trace 过期清理与仓储删除链路

## Modification

- `server/persistence/SqliteTraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/core/TraceRetentionService.h`
- `server/core/TraceRetentionService.cpp`
- `server/src/main.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `server/tests/TraceRetentionService_test.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/persistence/SqliteTraceRepository.cpp` 里补了中文注释，说明 retention 为什么只在 `trace_summary` 上判断过期，以及为什么删除顺序必须是 `trace_analysis -> trace_span -> trace_summary`。
- 在 `server/core/TraceRetentionService.h` 里补了中文注释，说明 `cleanup_in_progress_` 存在的原因，也就是防止后台重复投递清理任务。
- 在 `server/core/TraceRetentionService.cpp` 里补了中文注释，说明为什么提交任务前就要记录 `last_schedule_ms_`，以及为什么异步任务要通过 `shared_from_this()` 保住 service 生命周期。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 `log_retention_days` 先按冷启动配置消费，以及为什么启动清理和周期清理都只投递到 `query_tpool`，不在主线程直接删库。
- 在 `server/tests/SqliteTraceRepository_test.cpp` 里补了中文注释，说明 `DeleteExpiredTracesBatchDeletesOldestExpiredTracesFirst` 用例锁定的是“最老优先 + limit 生效”的删除语义。
- 在 `server/tests/TraceRetentionService_test.cpp` 里补了中文注释，说明为什么启动清理也必须受批次数预算控制，避免第一次启动就无限循环扫库。

## Learning Tips

### Newbie Tips

过期清理真正难的不是 SQL 能不能写出来，而是“删除粒度”先别搞错。既然前端、AI 分析和调用树都是 trace 级数据，那 retention 也必须按整条 trace 删，不能去 span 表里按时间乱扫，否则很容易删成半条树。

### Function Explanation

`TraceRetentionService` 负责的是“什么时候触发清理”，不是“怎么删表”。真正的三表事务删除还是留在 `SqliteTraceRepository`。这样以后前端列表页要补“删除单条 trace”按钮时，可以直接复用同一条仓储删除原语，不需要再写第二套删除逻辑。

### Pitfalls

后台任务如果 capture 的只是裸 `this`，而主线程退出时 service 比线程池先析构，就会留下非常典型的悬空指针。这里用 `shared_from_this()` 不是炫技，是为了确保“任务还在跑时对象不能先死”。

## Verification

- `cmake --build server/build --target test_sqlite_trace_repo`
- `cmake --build server/build --target test_trace_retention_service`
- `cmake --build server/build --target LogSentinel`
- `./server/build/test_sqlite_trace_repo`
- `./server/build/test_trace_retention_service`

## Verification Result

- `test_sqlite_trace_repo`：15/15 通过
- `test_trace_retention_service`：2/2 通过
- `LogSentinel`：编译通过

# 追加记录：2026-04-09 为 Trace AI 补总开关与执行状态骨架

## Git Commit Message

feat(trace): 为 ai 分析补总开关与执行状态骨架

## Modification

- `server/persistence/TraceTypes.h`
- `server/persistence/TraceRepository.h`
- `server/persistence/BufferedTraceRepository.h`
- `server/persistence/BufferedTraceRepository.cpp`
- `server/persistence/SqliteTraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `server/handlers/TraceQueryHandler.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/tests/LogHandler_test.cpp`
- `client/src/views/SettingsPrototype.vue`
- `client/src/types/trace.ts`
- `client/src/views/TraceExplorer.vue`
- `client/src/components/TraceListTable.vue`
- `client/src/components/AIAnalysisDrawerContent.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/persistence/TraceTypes.h` 里补了中文注释，说明 `ai_status/ai_error` 是 trace 汇总态，不等于 analysis 内容。
- 在 `server/persistence/TraceRepository.h` 和 `server/persistence/BufferedTraceRepository.h/.cpp` 里补了中文注释，说明为什么失败/跳过态不能伪造 analysis，以及为什么状态更新先走低频直透传。
- 在 `server/persistence/SqliteTraceRepository.h/.cpp` 里补了中文注释，说明为什么列表/详情都要直接返回 `ai_status`，以及为什么 analysis 成功时要把 `risk_level + ai_status` 一起回写。
- 在 `server/persistence/ConfigTypes.h` 和 `server/persistence/SqliteConfigRepository.cpp` 里补了中文注释，说明 `ai_analysis_enabled` 控制的是 worker 是否真发 AI，而不是把 provider 配置删掉。
- 在 `server/core/TraceSessionManager.h/.cpp` 里补了中文注释，说明为什么人工关闭 AI 只改 `ai_status=skipped_manual`，以及为什么 provider 缺失/异常不再伪造失败 analysis。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 `ai_analysis_enabled` 按冷启动配置消费，并直接决定是否创建 Trace AI provider。
- 在 `server/handlers/TraceQueryHandler.cpp` 里补了中文注释，说明为什么查询接口要把 `ai_status/ai_error` 一起吐给前端。
- 在 `server/tests/SqliteTraceRepository_test.cpp`、`server/tests/TraceSessionManager_unit_test.cpp` 和 `server/tests/LogHandler_test.cpp` 里补了中文注释，分别锁定了 pending/completed/skipped_manual 语义、人工关闭 AI 语义，以及 fake repo 对新接口的最小兼容语义。
- 在 `client/src/views/SettingsPrototype.vue`、`client/src/types/trace.ts`、`client/src/views/TraceExplorer.vue`、`client/src/components/TraceListTable.vue`、`client/src/components/AIAnalysisDrawerContent.vue` 里补了中文注释，说明总开关、状态枚举、前端兜底映射、列表状态列和详情页无分析态展示的原因。

## Learning Tips

### Newbie Tips

“没有 analysis”本身不是一个稳定状态，它可能代表处理中、人工关闭、熔断跳过或者调用失败。所以只靠 `analysis is null` 去猜执行态，前端一定会越写越乱。更稳的做法是把“执行态”单独落成 `ai_status`。

### Function Explanation

这次新增的 `UpdateTraceAiState` 不是在替代 `SaveAnalysisBatch`。它只负责没有 analysis 可写时的 summary 状态回写，例如人工关闭、熔断跳过、provider 不可用、主路失败。真正成功产出 analysis 的路径，还是继续走 `SaveAnalysisBatch`，只是顺手把 `ai_status` 收敛成 `completed`。

### Pitfalls

不要在 AI 调用失败时伪造一条 `AI_ANALYSIS_FAILED` 的分析记录。这会把“这条 trace 没分析过”和“这条 trace 分析过，只是结果特殊”混成一回事，前端和查询层后面都会越补越脏。

## Verification

- `cmake --build server/build --target test_sqlite_trace_repo`
- `cmake --build server/build --target test_trace_session_manager_unit`
- `cmake --build server/build --target test_log_handler`
- `cmake --build server/build --target LogSentinel`
- `./server/build/test_sqlite_trace_repo`
- `./server/build/test_trace_session_manager_unit`
- `./server/build/test_log_handler`
- `npm run build`

## Verification Result

- `test_sqlite_trace_repo`：16/16 通过
- `test_trace_session_manager_unit`：37/37 通过
- `test_log_handler`：5/5 通过
- `LogSentinel`：编译通过
- `npm run build`：仍未通过，但失败项都来自仓库既有前端 TS 问题；这次改动涉及的 `SettingsPrototype.vue`、`TraceExplorer.vue`、`TraceListTable.vue`、`AIAnalysisDrawerContent.vue`、`types/trace.ts` 没有新增构建错误

---

## Git Commit Message

feat(ai-proxy): 收口 trace 失败返回协议

## Modification

- `server/ai/proxy/main.py`
- `server/ai/proxy/providers/gemini.py`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/ai/proxy/main.py` 里补了中文注释，说明为什么 trace 路由失败不能再抛 HTTP 500 给 C++，而要收口成统一 JSON 协议；同时说明了新旧 provider 返回格式共存时为什么要在路由层做一次标准化。
- 在 `server/ai/proxy/providers/gemini.py` 里补了中文注释，说明为什么 Gemini SDK 异常不能再伪装成成功 analysis，而要提取 `code/status/message` 生成失败载荷。
- 在 `server/tests/ai_proxy_trace_protocol_test.py` 里补了中文注释，说明测试锁定的是 proxy 对 C++ 的外部契约，而不是某个具体 SDK 的内部实现。

## Learning Tips

### Newbie Tips

如果你准备在上游做熔断，那么“AI 请求失败”就必须先被定义成一条稳定协议。否则你今天拿到的是 HTTP 500 文本，明天拿到的是某家 SDK 的异常对象，后面的计数器和状态机都会越写越乱。

### Function Explanation

`normalize_trace_result` 的作用不是做业务分析，而是做协议适配。既然现在仓库里还存在旧 provider 返回 `analysis` 的写法，同时新协议又要引入 `ok/error_code/error_status/error_message`，那就应该在 proxy 路由边界做一次统一，不要把兼容逻辑散到 C++。

### Pitfalls

最脏的做法就是把 provider 调用失败伪造成一份“高风险 analysis”。这样前端看起来像是 AI 给了业务结论，实际上底下只是 SDK 调用炸了。业务结论和基础设施失败必须分开，否则后面根本没法做降级和熔断。

## Verification

- `timeout 15s python3 -m unittest server/tests/ai_proxy_trace_protocol_test.py; echo EXIT:$?`

## Verification Result

- `ai_proxy_trace_protocol_test.py`：3/3 通过，覆盖 trace 成功协议归一、失败协议归一、Gemini 错误字段提取三条路径

---

## Git Commit Message

feat(trace-ai): 接通 proxy 失败协议到 c++ 落库链路

## Modification

- `server/ai/TraceProxyProtocol.h`
- `server/ai/TraceProxyAi.cpp`
- `server/core/TraceSessionManager.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260409-feat-trace-prompt-consumption.md`

## 这次补了哪些注释

- 在 `server/ai/TraceProxyProtocol.h` 里补了中文注释，说明为什么 `ok=false` 不能再当成普通协议错误，而要转成稳定异常文本继续交给上层状态机处理。
- 在 `server/ai/TraceProxyAi.cpp` 里补了中文注释，说明为什么 JSON parse 完成后还要继续吃一层 trace 协议语义，而不是只做 HTTP 传输。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明 worker 捕获到的异常现在既可能是本地协议错误，也可能是 proxy 已归一好的 provider 失败文本，manager 只负责落 `failed_primary + ai_error`。
- 在 `server/tests/TraceSessionManager_unit_test.cpp` 里补了中文注释，分别锁定了 `TraceProxyProtocol` 的成功解析、失败解析，以及 proxy 失败文本如何继续落进 `ai_error`。

## Learning Tips

### Newbie Tips

跨语言边界最怕“半结构化失败”。如果 Python 一会儿回 HTTP 500 文本，一会儿回 JSON body，一会儿再把错误伪装成业务结果，那么 C++ 这边根本不可能稳定地做状态机。正确做法就是在边界上先统一协议，再让上层只消费统一语义。

### Function Explanation

`ParseTraceProxyResponseOrThrow` 做的是“协议翻译”，不是网络请求。它负责把 proxy 的成功 envelope 还原成 `TraceAiResponse`，把失败 envelope 变成一条可落库、可展示、可计数的异常文本。这样 `TraceProxyAi` 就只剩 HTTP 传输，`TraceSessionManager` 就只剩状态处理。

### Pitfalls

不要把 `ok=false` 继续当成“协议坏了”。如果 provider 因为 429、鉴权失败、配额耗尽而返回失败，这在业务上是一次真实 AI 调用失败，不是 JSON 结构损坏。把这两类失败混在一起，后面的前端展示和熔断策略都会被污染。

## Verification

- `cmake --build server/build --target test_trace_session_manager_unit`
- `./server/build/test_trace_session_manager_unit --gtest_filter='TraceProxyProtocolTest.*:TraceSessionManagerUnitTest.DispatchStoresNormalizedProxyFailureMessageAsAiError'`
- `cmake --build server/build --target LogSentinel`

## Verification Result

- `test_trace_session_manager_unit`：编译通过
- `TraceProxyProtocolTest.*` 与 `TraceSessionManagerUnitTest.DispatchStoresNormalizedProxyFailureMessageAsAiError`：3/3 通过
- `LogSentinel`：链接通过
