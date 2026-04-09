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
