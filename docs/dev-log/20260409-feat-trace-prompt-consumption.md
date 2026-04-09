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
