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

## 追加记录：benchmark CLI 开关第一刀

### Git Commit Message
`feat(server): 增加 benchmark CLI 实验开关`

### Modification
- `server/src/main.cpp`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

### What Changed
- 在 `main.cpp` 增加 `--disable-ai` 与 `--disable-webhook` 两个 CLI 开关，只服务 benchmark/对比实验，不进入正式 Settings。
- `--disable-ai` 的优先级收口为 `CLI > SQLite/Settings`：
  - 不自动拉起 proxy
  - 不构造 Trace AI provider
  - `TraceSessionManager` 启动时直接按 `ai_analysis_enabled=false` 语义收成 `skipped_manual`
- `--disable-webhook` 的优先级同样压过 Settings channel 和 CLI webhook override：
  - 不自动拉起本地 mock webhook
  - 不注入 CLI webhook 渠道
  - `notifier` 保持空，Trace 分析和落库链路继续正常工作
- 给黑盒脚本增加 `extra_args` 透传，补两条第三层场景：
  - `cli-disable-ai`：必须不打 fake proxy、`trace_analysis` 不新增、`ai_status=skipped_manual`
  - `cli-disable-webhook`：必须仍然完成 AI 分析，但不能有任何 webhook 外发
- 修正黑盒日志辅助函数：
  - 进程 `stdout` 管道改成累计缓冲
  - 避免先等 `Thread Model` 时把后续 `Trace AI disabled` 启动日志提前消费掉

### 中文注释
- `server/src/main.cpp`
  - 在 CLI 参数解析处补注释，说明这两个开关为什么只服务 benchmark，以及为什么优先级必须压过 Settings。
  - 在 webhook/notifier 分支补注释，说明“关闭通知外发”和“关闭整条分析链”是两回事。
- `server/tests/smoke_settings_blackbox.py`
  - 在 `start_server` 补注释，说明为什么要给进程挂 `_log_buffer`。
  - 在 `read_available_process_logs` 和 `wait_process_log_contains` 补注释，说明 `subprocess.PIPE` 是破坏性读取，黑盒必须自己维护累计日志。
  - 在两个新黑盒场景前补注释，明确它们锁的是 CLI 优先级，不是普通 Settings 冷启动消费。

### Verification
- `cmake --build server/build --target LogSentinel`
- `/home/llt/Project/llt/venv/bin/python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

### Learning Tips
#### Newbie Tips
- `subprocess.PIPE` 不是日志文件，它更像一条向前流动的管道。你读过一次，游标就前进了；后面再等另一个关键字时，之前那段字节不会自己回来。
- benchmark 开关最好和正式产品配置分开。否则你为了做“关 AI”的对照实验，还得先去改 SQLite 里的 `ai_analysis_enabled`，实验变量和业务配置就串味了。

#### Function Explanation
- `setattr(proc, "_log_buffer", "...")`：这里不是花活，而是给 `Popen` 对象挂一份测试态缓存，让多个日志断言共享同一份历史输出。
- `proc.poll()`：非阻塞检查子进程是否已经退出。黑盒里经常要边等日志边盯进程活性，这个接口正好做这件事。

#### Pitfalls
- 启动日志断言如果只看“本次新读到的增量”，很容易出现假阴性：实际上后端已经打印过了，只是前一个断言先把那段输出吃掉了。
- `--disable-webhook` 这种开关如果写成“直接不创建 notifier”还不够，你还得同时挡掉 mock webhook 注入和 CLI override，不然还是会偷偷发通知。

## 追加记录：no-buffer benchmark 开关

### Git Commit Message
`feat(server): 增加 no-buffer benchmark 开关`

### Modification
- `server/persistence/TraceWriteSink.h`
- `server/persistence/DirectTraceWriteSink.h`
- `server/persistence/DirectTraceWriteSink.cpp`
- `server/persistence/BufferedTraceRepository.h`
- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `server/CMakeLists.txt`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

### What Changed
- 抽了一个最小 `TraceWriteSink` 写入口接口，只保留：
  - `AppendPrimary`
  - `AppendAnalysis`
  - `UpdateTraceAiState`
- 让 `BufferedTraceRepository` 继续实现这套接口，保持默认主链完全不变。
- 新增 `DirectTraceWriteSink`，作为 `--disable-buffered-trace-repo` 的 no-buffer 对照组实现：
  - primary 走 `SaveSingleTraceAtomic`
  - analysis 走 `SaveSingleTraceAnalysis`
  - 失败/跳过态走 `UpdateTraceAiState`
- 把 `TraceSessionManager` 从“只认 `BufferedTraceRepository*`”改成“只认 `TraceWriteSink*`”，这样 manager 不再关心底层到底是双缓冲还是直写 SQLite。
- 在 `main.cpp` 接入 `--disable-buffered-trace-repo`：
  - 默认模式仍然构造 `BufferedTraceRepository`
  - 打开开关时切到 `DirectTraceWriteSink`
  - 启动日志新增 `Trace persistence mode: buffered/direct-no-buffer`
- 为第三层黑盒新增 no-buffer 场景，锁定：
  - CLI 开关生效
  - trace 仍能收成 `completed`
  - `trace_analysis` 仍能写出

### 中文注释
- `server/persistence/TraceWriteSink.h`
  - 补注释说明为什么这个接口只抽三条写入口，不把查询和 flush 细节也一起抽进去。
- `server/persistence/DirectTraceWriteSink.h`
  - 补注释说明它是 benchmark 的 no-buffer 对照组，而不是新的正式产品模式。
- `server/persistence/DirectTraceWriteSink.cpp`
  - 在 `AppendPrimary` 和 `AppendAnalysis` 补注释，说明为什么 no-buffer 仍然要守住 primary 原子性、以及为什么要保留空 analysis 兜底。
- `server/core/TraceSessionManager.h`
  - 补注释说明 manager 为什么只能依赖写入口接口，不能把具体持久化实现耦合回状态机。
- `server/core/TraceSessionManager.cpp`
  - 补注释说明“链路可用”的判断已经从 `BufferedTraceRepository` 收口成 `TraceWriteSink`。
  - 补注释说明 primary 先进入持久化写入口的语义不变，只是底层实现可能变成 no-buffer。
- `server/src/main.cpp`
  - 补注释说明 `--disable-buffered-trace-repo` 的目标是拿掉缓冲写入器，而不是关掉持久化。
- `server/tests/smoke_settings_blackbox.py`
  - 补注释说明 no-buffer 场景验证的是写入口切换。
  - 补注释说明为什么要显式把 provider 再钉回 `glm`，避免前面场景的 provider 状态残留串味。

### Verification
- `cmake --build server/build --target LogSentinel`
- `/home/llt/Project/llt/venv/bin/python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

### Learning Tips
#### Newbie Tips
- “关缓冲”不等于“直接把缓冲对象传空”。如果上层状态机直接依赖具体类型，你想做对照组时就会被迫在业务代码里撒一堆 `if (no_buffer)`，最后实验开关把主链写烂。
- 对照实验最好切在“同一层职责边界”上。这次切的是写入口实现，而不是数据库 schema、Trace 聚合逻辑或 AI 协议，所以实验变量才干净。

#### Function Explanation
- `SaveSingleTraceAtomic`：把一条 trace 的 summary 和 spans 放进同一个事务里写入，避免只写进去半条主数据。
- `std::shared_ptr<TraceWriteSink>`：这里让启动期决定“装哪种写入口实现”，后面运行态就只认同一份多态对象，不需要把实验开关一路传到 worker 里。

#### Pitfalls
- 黑盒新增场景时，最容易犯的错不是生产代码，而是沿用了上一场景残留的 provider 配置。这次场景 10 一开始就踩了这个坑，表面看像 no-buffer 失败，实际上是测试自己把 `mock` 和 `glm` 搅混了。
- 如果 shutdown 统计还无脑去解引用 `buffered_trace_repo`，切到 no-buffer 后进程退出时就会直接空指针崩掉，所以这里必须把 shutdown 打印分成 buffered/no-buffer 两条分支。

## 追加记录：ai_model / ai_api_key 热更新第一刀

### Git Commit Message
`feat(ai): 支持模型与密钥热更新`

### Modification
- `server/persistence/SqliteConfigRepository.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/ai/TraceAiFactory.h`
- `server/ai/TraceAiFactory.cpp`
- `server/ai/TraceProxyAi.h`
- `server/ai/TraceProxyAi.cpp`
- `server/src/main.cpp`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

### What Changed
- 在 `SqliteConfigRepository` 增加专门给 Trace AI 用的运行时版本号，只在 `ai_model / ai_api_key` 更新时递增。
- 把配置发布顺序改成“先 COMMIT，再发布内存快照/版本号”，避免运行态先看到数据库还没提交成功的值。
- 在 `TraceAiFactory` 增加运行时版本读取器和凭证读取器，让工厂继续只负责组装，不负责热更新策略本身。
- 在 `TraceProxyAi` 增加“本地不可变小快照”：
  - 每次 `AnalyzeTrace` 先看版本号；
  - 版本没变就继续用本地 `model/api_key`；
  - 版本变了才重新抓一次凭证并原子换代。
- 在 `main.cpp` 只给主 provider 挂上这条热更新链，明确收口为“只热更新请求体字段，不热更新 provider 路由”。
- 在第三层黑盒增加一条“运行中改 `ai_model / ai_api_key`、不重启后端”的场景，直接证明下一条 trace 请求会带新值。

### 中文注释
- `server/persistence/SqliteConfigRepository.h`
  - 在 `trace_ai_runtime_version_` 旁补注释，说明这条版本线只服务 `model/api_key` 热更新，不碰 provider。
- `server/persistence/SqliteConfigRepository.cpp`
  - 在 `TouchesTraceAiRuntimeHotKeys` 补注释，说明为什么这条版本线只盯主 provider 的请求体字段。
  - 在 `handleUpdateAppConfig` 的发布顺序处补注释，说明为什么必须“先提交再发布”。
- `server/ai/TraceAiFactory.h`
  - 在运行时 reader 字段旁补注释，说明工厂只绑定热更新数据源，不切 provider 路由。
- `server/ai/TraceProxyAi.h`
  - 在 `runtime_request_config_` 旁补注释，说明为什么并发场景下不能原地改共享字符串。
- `server/ai/TraceProxyAi.cpp`
  - 在 `GetRuntimeRequestConfig` 补注释，说明为什么只在版本变化时刷新，以及并发重复刷新为什么可接受。
- `server/src/main.cpp`
  - 在热更新 reader 接线处补注释，说明这里只热更新 `model/api_key`，不把整份 Settings 热路径化。
- `server/tests/smoke_settings_blackbox.py`
  - 在新黑盒场景前补注释，说明它锁的是“运行中热更新”而不是原来的冷启动消费。
  - 在 webhook 清理处补注释，说明为什么要避免场景之间互相污染。

### Verification
- `cmake --build server/build --target LogSentinel`
- `/home/llt/Project/llt/venv/bin/python3 server/tests/smoke_settings_blackbox.py`

### Learning Tips
#### Newbie Tips
- 热更新不等于“把成员变量改一下”。只要同一个对象会被多个线程并发调用，原地改共享字符串就是数据竞争。
- 版本号的真正作用不是存历史，而是帮你把“绝大多数没变化的请求”挡在快路径里，只在变更发生时才重抓配置。
- `provider` 和 `model/api_key` 不是一类东西。前者决定打哪个路由，后者只是这次请求体里带什么参数，所以它们不应该用同一种热更新策略。

#### Function Explanation
- `std::atomic_load_explicit(&shared_ptr, std::memory_order_acquire)`：原子读出当前发布的小快照，让读线程拿到一份稳定对象，而不是半写状态。
- `std::atomic_store_explicit(&shared_ptr, ..., std::memory_order_release)`：一次性发布新的不可变快照；旧快照会在最后一个读者放手后自然释放。
- `fetch_add(1, std::memory_order_acq_rel)`：给版本号做单调递增，既表达“确实发生过更新”，也给读线程一个便宜的变化探针。

#### Pitfalls
- 如果先发布新快照，再去做 SQLite `COMMIT`，一旦事务提交失败，内存态和持久化态就会分叉，后面排查会非常恶心。
- 黑盒里新增 trace 场景时，要注意它会不会顺手污染 webhook 或 provider 的探针状态；这次就因为新场景默认被 fake proxy 识别成 `critical`，把后面的阈值断言串脏了一次。

## 追加记录：fallback model/api_key 热更新

### Git Commit Message
`feat(ai): 支持降级模型与密钥热更新`

### Modification
- `server/persistence/SqliteConfigRepository.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/ai/TraceAiFactory.h`
- `server/src/main.cpp`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `CurrentTask.md`

### What Changed
- 扩展 `trace_ai_runtime_version_` 的命中范围，把 `ai_fallback_model / ai_fallback_api_key` 也纳入同一条 AI 请求体热更新版本线。
- 保持 `ai_fallback_provider` 冷启动不变，只让 fallback provider 的请求体参数支持运行中刷新。
- 在 `main.cpp` 给 fallback `TraceProxyAi` 也挂上 `runtime_version_reader + runtime_credentials_reader`，但 reader 读取的是 fallback 字段而不是主路字段。
- 在第三层黑盒增加一条 `gemini -> glm` 降级场景：先冷启动固定主备路由，再运行中修改 `ai_fallback_model / ai_fallback_api_key`，验证下一次 fallback 请求必须带新值。

### 中文注释
- `server/persistence/SqliteConfigRepository.h`
  - 把版本号注释修正成“主路与 fallback 的请求体热更新都覆盖”，避免继续误导成只主路有效。
- `server/persistence/SqliteConfigRepository.cpp`
  - 在 `TouchesTraceAiRuntimeHotKeys` 补注释，说明为什么 fallback 的 `model/api_key` 也要纳入同一条版本线。
- `server/ai/TraceAiFactory.h`
  - 把 runtime reader 的注释修正成“当前 provider 实例对应的凭证来源”，不再暗示只给主路用。
- `server/src/main.cpp`
  - 在 fallback reader 接线处补注释，说明这里只热更新 fallback 请求体字段，不热更新 fallback provider 路由。
- `server/tests/smoke_settings_blackbox.py`
  - 在新黑盒场景前补注释，说明必须先冷启动定 provider，再运行中只改 fallback 凭证。

### Verification
- `cmake --build server/build --target LogSentinel`
- `/home/llt/Project/llt/venv/bin/python3 server/tests/smoke_settings_blackbox.py`

### Learning Tips
#### Newbie Tips
- 同一条版本号线不一定代表“同一份配置对象”。这次主路和 fallback 复用的是“变化探针”，真正读取哪组字段还是由各自的 reader 决定。
- 如果路由是冷启动、凭证是热更新，那么测试一定要先把路由固定住，再在不重启的情况下改凭证；不然你根本分不清到底是路由生效了还是热更新生效了。

#### Function Explanation
- `runtime_credentials_reader`：把“这个 provider 该从哪组配置里拿 model/api_key”延迟到运行时决定，但仍然只在版本变化时调用。

#### Pitfalls
- 不要把 build 和“会反复重启同一个二进制”的黑盒并行跑。链接器改写可执行文件时，黑盒刚好在 `execve`，就会报 `Permission denied`，这次已经踩过一次。
