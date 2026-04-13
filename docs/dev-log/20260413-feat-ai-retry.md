# 2026-04-13 feat-ai-retry

## Git Commit Message
`feat(ai): 落地 Trace AI 重试主链`

## Modification
- `server/ai/proxy/schemas.py`
- `server/ai/proxy/main.py`
- `server/ai/proxy/providers/gemini.py`
- `server/ai/proxy/providers/glm.py`
- `server/ai/TraceAiFactory.h`
- `server/ai/TraceAiFactory.cpp`
- `server/ai/TraceProxyAi.h`
- `server/ai/TraceProxyAi.cpp`
- `server/src/main.cpp`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

## What Changed
- 给 Trace proxy 请求 schema 增加 `retry_enabled / retry_max_attempts`，让 C++ 冷启动配置能真正进入 Python proxy。
- 在 `server/ai/proxy/main.py` 增加统一重试执行器，语义收口为：
  - `ai_timeout_ms` 是单个 provider 调用链总预算；
  - 第一枪拿满原始预算，后续 attempt 只吃剩余预算；
  - `429 / 5xx / 网络错误` 可重试；
  - `PROVIDER_FORMAT_ERROR / PROVIDER_SCHEMA_ERROR / INVALID_PROVIDER_RESPONSE` 只额外补一枪；
  - `4xx` 鉴权/参数错误和 `TIMEOUT` 不重试。
- 给 `Gemini/GLM` 失败载荷补 `http_status`，并把 GLM 的无响应网络问题单独归类成 `NETWORK_ERROR`，避免和真正的 HTTP 4xx/5xx 混淆。
- 给 `TraceAiFactory / TraceProxyAi / main.cpp` 接上 `retry_enabled / retry_max_attempts` 冷启动透传，并把这两个值写进启动日志。
- 为 Python proxy 补红灯测试，锁定路由透传、预算共享、`401` 不重试、结构错误只补一枪。

## Verification
- `python3 -m unittest server/tests/ai_proxy_trace_protocol_test.py`
- `cmake --build server/build --target LogSentinel test_trace_session_manager_unit`
- `./server/build/test_trace_session_manager_unit --gtest_filter=TraceProxyProtocolTest.*:TraceProxyTransportErrorTest.*`
- `git diff --check`

## Learning Tips
### Newbie Tips
- “总预算”不是“每次请求都发一份新的 timeout”。如果重试实现里每次都把 `timeout_ms` 原样传给 provider，那么设置页里的总预算语义就是假的。
- 结构化输出失败不一定等于请求错了。很多时候只是模型这一枪吐歪了，所以可以补一枪；但不能一路补到 `max_attempts`，否则会把稳定的 prompt/schema 设计问题遮住。
- `429` 虽然是 `4xx`，但它表达的是“现在打太快了”，不是“请求内容有错”，所以通常应该进可重试分支。

### Function Explanation
- `run_in_threadpool`：把同步 provider 调用丢到框架线程池执行，避免在 FastAPI `async` 路由里直接跑阻塞网络请求。
- `asyncio.sleep`：在协程里做退避等待，不阻塞事件循环；这和 `time.sleep` 的区别在于后者会把整个 event loop 卡住。
- `inspect.isawaitable`：这里用来兼容“生产里传异步 sleep、测试里传同步 fake sleep”两种调用方式，避免为了测试改坏生产代码结构。

### Pitfalls
- Python 默认参数会在定义时绑定。如果把 `call_provider_in_threadpool` 直接写成函数默认值，测试里后续 monkeypatch 不会生效，这次就踩到了这个坑。
- 重试分类不能只看 `error_status` 字符串。真实 provider 经常同时有 HTTP 状态码和厂商业务码，重试层应该优先认 `http_status`，否则 `429` 很容易被漏判。
- 如果第一枪也按“剩余预算”重新计算，很容易因为取时钟的细微差值把 `30000` 变成 `29999`，既让测试抖动，也让语义看起来不稳定。
