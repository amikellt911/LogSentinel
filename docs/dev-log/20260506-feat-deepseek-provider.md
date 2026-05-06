# 20260506-feat-deepseek-provider

## Git Commit Message

feat(ai): 接入 DeepSeek Trace Provider

## Modification

- `server/ai/proxy/providers/deepseek.py`
- `server/ai/proxy/main.py`
- `server/ai/TraceAiBackend.h`
- `server/src/main.cpp`
- `client/src/views/SettingsPrototype.vue`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_TraceAiProvider.md`

## Learning Tips

### Newbie Tips

新增 provider 时不要把厂商 HTTP 细节塞进 C++ 主链。既然当前链路是 `TraceProxyAi -> Python proxy -> provider`，那么 C++ 只需要知道路由段，DeepSeek 的 base_url、Bearer 鉴权、错误体和 usage 字段都应该留在 Python provider 里。

DeepSeek 的 JSON Output 是 `response_format={"type":"json_object"}`，它只能提高模型输出 JSON 对象的概率，不等于服务端按业务 schema 强校验。所以 provider 拿到 `choices[0].message.content` 后仍然要 `json.loads`，接着用 `LogAnalysisResult.model_validate` 校验字段。

provider 路由仍然是冷启动语义。运行中保存 Settings 只会热更新同一路由下的 `model/api_key`，不会把已经创建的 `gemini` provider 对象切成 `deepseek`。

### Function Explanation

`httpx.Client.post()`：同步 HTTP 调用。当前 provider 接口还是同步方法，FastAPI 路由层会把它丢进线程池，provider 内部不再额外引入 async 生命周期。

`response.raise_for_status()`：在非 2xx 响应时抛 HTTP 异常。本次仍先显式处理 `status_code >= 400`，这样可以解析 DeepSeek 原始错误体并保留 `http_status` 给 retry 层判断。

`pydantic.BaseModel.model_validate()`：把模型输出的 dict 校验成业务结构。这里用于防止模型返回缺字段、字段类型错误或 risk_level 不合法时被误落成成功分析。

### Pitfalls

不要继续写旧模型名 `deepseek-chat` / `deepseek-reasoner`。本次默认使用 `deepseek-v4-flash`，设置页提示也改成这个模型。

不要重复拼接 `trace_text`。C++ 已经把 trace payload 和 prompt 模板交给 proxy，proxy 的 `render_trace_prompt()` 会注入 trace，上游 provider 只需要发送最终 prompt。

不要把 `TIMEOUT` 和 4xx 混成同一类错误。网络抖动和 429/5xx 可以交给 retry 层，鉴权失败这类确定性错误不应该靠重试掩盖。

## Verification

- `python3 server/tests/ai_proxy_trace_protocol_test.py -k deepseek -v`：通过，1 条测试通过。
- `python3 server/tests/ai_proxy_trace_protocol_test.py -v`：通过，14 条测试通过。
- `cmake --build server/build --target test_trace_session_manager_unit`：通过。
- `./server/build/test_trace_session_manager_unit`：通过，54 条测试通过。
- `./server/build/test_trace_session_manager_integration`：通过，14 条测试通过。
- `./server/build/test_http_context`：通过，10 条测试通过。
- `python3 -m py_compile server/ai/proxy/providers/deepseek.py server/ai/proxy/main.py`：通过。
- `npm run build`：未通过，失败在既有 TypeScript 问题，涉及 `AIEngineSearchBar.vue`、`BatchArchiveList.vue`、`BusinessHealthCards.vue`、`PromptDebugger.vue`、`RiskDistribution.vue`、`TraceWaterfall.vue`、`AIEngine.vue`、`Settings.vue`。
- `python3 server/tests/smoke_settings_blackbox.py --server-bin ./server/build/LogSentinel --ready-timeout 15 --dispatch-timeout 15`：未通过。DeepSeek 主路黑盒场景已经执行到后续阶段，最终失败在既有 `trace_lifecycle_profile=` 启动日志等待点。
