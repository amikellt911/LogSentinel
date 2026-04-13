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

## 追加记录：第三层黑盒收尾

### Modification
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

### What Changed
- 给 `smoke_settings_blackbox.py` 的本地探针补顺序响应队列，让同一个 provider 可以按“第一次失败、第二次成功”的顺序吐行为。
- 把本地探针从“静态 fake proxy”升级成“会复用真实 `execute_trace_provider_with_retry` 的 mini proxy”，这样黑盒验证的就是生产里的重试执行器，而不是测试脚本自己手搓的一份假逻辑。
- 新增两条第三层黑盒：
  - `429 -> success`：必须看到同一个 provider 被请求两次，最终 `completed`
  - `401 -> success(诱饵)`：必须只打一枪，最终 `failed_primary`

### Verification
- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

### Newbie Tips
- 黑盒测试如果把被测模块绕开了，就算跑通也没价值。这次一开始的 fake proxy 直接替代了 Python proxy，结果只能看到一条外层 HTTP 请求，根本证明不了 retry 是否真的发生。
- “诱饵成功响应”是很有用的黑盒技巧。像 `401` 不重试这种场景，把第二个响应故意设成 success，能防止测试只会验证“失败了”，却验证不了“中间有没有偷偷重试”。

### Pitfalls
- 如果探针只支持固定响应，不支持顺序队列，那么 `429 -> success` 这种场景就永远写不出来，最后只能退化成单元测试，覆盖不到冷启动配置和真实 HTTP 链路。
- 顺序队列必须在每次重新设静态行为时清空；否则前一个场景残留的第二枪响应会串到下一个场景，黑盒会出现非常难排查的假阳性或假阴性。

## 追加记录：单入口部署

### Git Commit Message
`feat(server): 收口前后端单入口部署`

### Modification
- `server/handlers/FrontendAssetHandler.h`
- `server/handlers/FrontendAssetHandler.cpp`
- `server/tests/FrontendAssetHandler_test.cpp`
- `server/src/main.cpp`
- `server/tests/smoke_settings_blackbox.py`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

### What Changed
- 新增 `FrontendAssetHandler`，把交付态前端托管语义收口成三层：
  - 真实静态文件优先直出；
  - 白名单页面 `/`、`/service`、`/traces`、`/settings` fallback 到 `index.html`；
  - 非白名单未知路径返回“不处理”，交给上层统一 404。
- 在 `main.cpp` 增加 `--frontend-dist`，并补默认探测逻辑：
  - 先按可执行文件目录推导 `client/dist`
  - 再回退到当前工作目录候选
- 把入口分流改成：
  - `/api/*` 先去掉 `/api` 前缀再走现有 Router
  - 裸 API 继续兼容
  - 非 API 再交给 `FrontendAssetHandler`
  - API 未命中和未知页面都保持 404
- 给黑盒脚本补临时 `frontend-dist` 和 5 条单入口断言，证明同源静态页面、静态资源、`/api/settings/all` 都已经可用。

### Verification
- `cmake --build server/build --target LogSentinel test_frontend_asset_handler`
- `./server/build/test_frontend_asset_handler`
- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

### Learning Tips
#### Newbie Tips
- SPA 的 “history 路由 fallback” 不是“所有未知路径都回 `index.html`”。如果你不做白名单，`/fdasxz` 这种瞎输路径也会显示成功页面，排障会非常恶心。
- `/api/*` 前缀剥离一定要在前端 fallback 之前做。否则 `/api/settings/all` 一旦没命中后端路由，就可能被误回前端壳页面，接口调用会变成诡异的 200 HTML。
- 黑盒里最好自己造一份极小的 `dist`。这样验证的是后端托管逻辑本身，而不是开发机恰好残留了一份可用构建产物。

#### Function Explanation
- `std::filesystem::weakly_canonical`：把路径里的 `.`、`..` 和可解析部分折叠成规范路径；这里拿它做“请求路径最终是否还在 dist 根目录下”的安全判断。
- `std::ifstream(..., std::ios::binary)`：按二进制读静态文件，避免不同平台的文本模式偷偷改写换行或字节内容。

#### Pitfalls
- 把“构建二进制”和“需要多次重启执行同一个二进制”的黑盒并行跑，会撞上链接器重写目标文件，进程二次拉起时就可能报 `Permission denied`。这种验证必须串行。
- `start_server` 这类测试辅助函数一旦新增位置参数，所有调用点都要优先改成命名参数，否则很容易把 `frontend_dist`、`trace_ai_base_url` 这种字符串参数串位，错误表象会非常怪。
