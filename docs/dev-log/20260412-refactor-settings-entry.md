# Git Commit Message

refactor(frontend): 收口设置正式入口并补单入口部署目标

# Modification

- `client/src/router/index.ts`
- `client/src/layout/MainLayout.vue`
- `CurrentTask.md`
- `docs/todo-list/Todo_Settings_MVP5.md`

# What Changed

- 将正式设置入口统一收口到 `/settings -> SettingsPrototype`。
- 保留 `/settings-prototype -> /settings` 重定向，兼容旧书签与联调阶段遗留地址。
- 从左侧导航移除独立的 `settings-prototype` 菜单项，避免继续暴露双设置页语义。
- 在 `CurrentTask.md` 中补入“后端托管 `client/dist`，形成单入口部署”的 v1.0.0 目标，并标记 `/settings` 单入口已完成。

# Verification

- `git diff --check`
- `cd client && npm run build`

说明：
- `git diff --check` 已通过，没有新增格式问题。
- `npm run build` 仍失败，但失败点是既有前端 TS 历史债，不是本次路由/侧边栏收口引入的新问题。
- 当前已确认仍存在的旧错误文件：
  - `client/src/components/AIEngineSearchBar.vue`
  - `client/src/components/BatchArchiveList.vue`
  - `client/src/components/BusinessHealthCards.vue`
  - `client/src/components/PromptDebugger.vue`
  - `client/src/components/RiskDistribution.vue`
  - `client/src/components/TraceWaterfall.vue`
  - `client/src/views/AIEngine.vue`
  - `client/src/views/Settings.vue`

# Learning Tips

## Newbie Tips

- SPA 正式部署通常只有一个 `index.html`。像 `/settings`、`/service` 这种路径不是不同 HTML 文件，而是同一个入口页面交给前端路由继续分发。
- 如果一个原型页已经转正，就不要继续把“旧页面 + 原型页”同时暴露给用户。双入口会把产品语义搞脏，后面联调和答辩都要反复解释。

## Function Explanation

- `redirect`：Vue Router 里的重定向规则。这里用它兼容旧地址，让历史书签自动跳回正式入口。
- `computed`：根据当前路由动态计算标题。这里保留它，是为了让顶部标题始终跟正式路由语义一致。

## Pitfalls

- 只改路由不改侧边栏，用户还是会从导航点进旧入口，等于没有真正收口。
- 只改前端入口但不更新任务文档，后面再做“单入口部署”时很容易忘掉这一步已经完成，导致文档和代码状态不一致。

---

# Git Commit Message

test(settings): 补配置真实生效第二层自动化测试

# Modification

- `server/tests/LogHandler_test.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 为 `LogHandler` 新增 3 条设置消费测试：
  - 自定义 `trace_end_field` 能触发 sealed + dispatch
  - `trace_end_aliases` 能触发 sealed + dispatch
  - 无关字段不会被误判成结束字段
- 为 `TraceSessionManager` 新增 1 条设置消费测试：
  - 即使传入 fallback provider，只要 `ai_auto_degrade=false`，主路失败后也不能调用备路
- 补齐 `LogHandler` 测试里的最小 fake repo 记录能力，专门观察主数据是否被 dispatch 出去
- 复跑整套 `test_log_handler` 与 `test_trace_session_manager_unit`，确认设置第二层 `gtest` 没有引入回归

# Verification

- `cmake --build server/build --target test_log_handler test_trace_session_manager_unit`
- `./server/build/test_log_handler`
- `./server/build/test_trace_session_manager_unit`

结果：
- `test_log_handler`：`8/8` 通过
- `test_trace_session_manager_unit`：`45/45` 通过

# Learning Tips

## Newbie Tips

- 配置消费测试不要依赖真实定时器。只要定时器还在跑，你就分不清到底是设置生效导致的状态变化，还是 idle timeout 自己把会话收走了。
- 这类测试最稳的写法是“真主链对象 + 假外部依赖”。比如 `LogHandler/TraceSessionManager` 用真的，AI/Webhook 用桩。

## Function Explanation

- `SweepExpiredSessions(...)`：手动推进时间轮，把 collecting/sealed/retry_later 这三类会话往下推进。测试里直接调它，能把时序完全控死。
- `SavePrimaryBatch(...)`：`BufferedTraceRepository` 后台 flush 主数据时会走的仓储接口。这里 fake repo 记录它，就能判断 trace 是否真的被 dispatch 到主数据写入阶段。

## Pitfalls

- 只看 `handleTracePost()` 的 HTTP 202 不足以证明结束字段生效，因为请求被接住并不等于 trace 已经 sealed。
- 给 `TraceSessionManager` 传了 fallback provider，也不代表自动降级就会发生。真正决定会不会切备路的是 `ai_auto_degrade_enabled` 这个开关。

---

# Git Commit Message

test(settings): 补第三层黑盒联调并修复 skipped_manual 落库竞态

# Modification

- `server/tests/smoke_settings_blackbox.py`
- `server/tests/TraceSessionManager_integration_test.cpp`
- `server/core/TraceSessionManager.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 新增 `server/tests/smoke_settings_blackbox.py`，走真实后端进程做第三层黑盒联调：
  - 先通过 `/settings/config` 写入 `http_port / trace_end_aliases / ai_analysis_enabled`
  - 再重启后端
  - 最后通过端口切换、alias 触发 dispatch、`trace_summary.ai_status=skipped_manual` 证明冷启动配置已被真实消费
- 在 `TraceSessionManager_integration_test.cpp` 补了一条真实 SQLite 集成测试，锁定 `ai_analysis_enabled=false` 时 `trace_summary.ai_status` 不能停留在 `pending`
- 修复 `TraceSessionManager.cpp` 中人工关闭 AI 的落库时机：把 `skipped_manual` 直接写进 prepared summary，再跟 primary 一起入缓冲，避免被异步 flush 顺序覆盖回默认 `pending`

# Verification

- `cmake --build server/build --target test_trace_session_manager_integration`
- `./server/build/test_trace_session_manager_integration --gtest_filter='TraceSessionManagerIntegrationTest.AiDisabledPersistsSkippedManualStatusWithBufferedRepository'`
- `cmake --build server/build --target LogSentinel`
- `python3 server/tests/smoke_settings_blackbox.py`

结果：
- 新增的 SQLite 集成测试先红后绿，最终通过
- `LogSentinel` 已重编通过
- 黑盒脚本最终通过，确认：
  - `http_port` 重启后切换到新端口
  - `trace_end_aliases` 能触发真实 dispatch
  - `ai_analysis_enabled=false` 会把 `trace_summary.ai_status` 落成 `skipped_manual`

# Learning Tips

## Newbie Tips

- 黑盒测试最值钱的地方，不是“又写了一层脚本”，而是它能把 fake repo 和单元桩看不到的跨线程落库顺序问题直接打出来。
- 只要主数据是异步 flush、状态更新是同步 UPDATE，你就要立刻警惕“UPDATE 跑在 INSERT 前面”的竞态；不然数据库最后看到的往往是默认值，不是你以为已经改掉的最终状态。

## Function Explanation

- `trace_summary.ai_status`：当前主表里 AI 状态的最终展示口径。列表页和详情页都直接读它，所以它如果停在 `pending`，前端看到的就一定是错的。
- `prepared_summary`：`TraceSessionManager` 在 dispatch 前构造的摘要缓存。只要某个最终状态在 dispatch 时已经确定，就应该优先写回这里，再和 primary 一起入缓冲。

## Pitfalls

- 只在 unit test 里看 `UpdateTraceAiState()` 被调用过，不等于 SQLite 最终真写对了。真正的竞态通常发生在“有没有调用”之外，而是在“调用和插入谁先发生”。
- 修完库内竞态后，如果黑盒还红，先确认你跑的到底是不是新二进制；这次就出现过“测试目标重链了，但 `LogSentinel` 还没重链”的假回归。

---

# Git Commit Message

fix(core): 彻底修复 UpdateTraceAiState 竞态，改用双缓冲队列同步落库

# Modification

- `server/persistence/TraceRepository.h`
- `server/persistence/BufferedTraceRepository.h`
- `server/persistence/BufferedTraceRepository.cpp`
- `server/persistence/SqliteTraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/core/TraceSessionManager.cpp`
- `server/tests/TraceSessionManager_integration_test.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- **撤销短视 Hack**：从 `TraceSessionManager.cpp` 移除了强制覆写 `prepared_summary->ai_status = kAiStatusSkippedManual` 的奇技淫巧。
- **引入状态缓冲机制**：在 `BufferedTraceRepository` 的 `AnalysisBufferGroup` 结构里新增 `std::vector<TraceAiStateWrite> ai_states`，把单点透传改成推入缓冲队列。
- **重构持久层批量接口**：
  - `TraceRepository` 和 `SqliteTraceRepository` 新增 `UpdateTraceAiStateBatch` 方法。
  - `SqliteTraceRepository` 内部使用 `BEGIN TRANSACTION; ... COMMIT;` 对多条状态更新进行批量执行，避免多次单条 `UPDATE` 造成的大量 fsync。
- **锁定 Flush 时序**：在 `BufferedTraceRepository::FlushLoop` 中，强制先通过 `SavePrimaryBatch` 处理 `PrimaryBuffer` (INSERT)，然后才会处理包含 `UpdateTraceAiStateBatch` 的 `AnalysisBuffer` (UPDATE)。彻底消除 `UPDATE` 先于 `INSERT` 执行导致的 `pending` 竞态。
- **修复自动化测试 Flaky 问题**：将 `test_trace_session_manager_unit` 中熔断冷却测试的 `ai_cooldown_ms` 拉长至 2000ms，并在等待期间增加 2100ms 休眠，避免因状态更新进入缓冲导致的延迟可见性问题吃掉短冷却时间，引起二次断言失败。
- **修复黑盒断言轮询**：在 `TraceSessionManager_integration_test.cpp` 引入 `while` 循环轮询等 `skipped_manual` 状态落库，而不是期待它瞬间同步可用。

# Verification

- `cmake --build server/build --target test_trace_session_manager_unit test_trace_session_manager_integration`
- `./server/build/test_trace_session_manager_integration --gtest_filter='TraceSessionManagerIntegrationTest.AiDisabledPersistsSkippedManualStatusWithBufferedRepository'`
- `./server/build/test_trace_session_manager_unit`
- `python3 server/tests/smoke_settings_blackbox.py`

结果：
- 单元测试（45/45）与集成测试（1/1）全部通过。
- Python 黑盒联调全部通过。

# Learning Tips

## Newbie Tips

- **状态更新也必须进缓冲排队**。只要主数据是异步写入的，后续任何对此数据的单点更新都不能走同步直连 DB，必须老老实实跟着主数据的流水线排队处理，否则就会遇到“你以为改了，但它最终还是初始值”的覆盖竞态。
- **治标不治本的 Hack 会掩盖架构缺陷**。不要在业务调用层去强行拼凑数据时序（比如提前注入状态），时序问题必须在数据底层的缓冲队列里统一解决。

## Function Explanation

- `UpdateTraceAiStateBatch`：在抽象类中引入的批量更新接口。相比逐条 UPDATE，批量更新能极大减少 SQLite 写 WAL 的 fsync 次数，不仅解决了线程间时序竞争，还附带了性能红利。

## Pitfalls

- **测试用例的时序强耦合**：单元测试中的时间等待非常容易受到真实架构变动（例如增加队列缓冲导致的延迟）影响。当状态更新从“同步立即可见”变为“异步稍微延迟可见”时，原来的死等或过短的冷却时间都会变成偶现失败的 Flaky Test，需要相应调长超时/冷却配置。

---

# Git Commit Message

test(settings): 补 prompt 与 webhook channel 第三层黑盒联调

# Modification

- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 扩展 `server/tests/smoke_settings_blackbox.py`，在原有 `http_port / trace_end_aliases / ai_analysis_enabled` 黑盒基础上，继续覆盖：
  - `prompt / active_prompt_id`：通过本地假 AI proxy 记录后端实发 `prompt/model/api_key`
  - `webhook channel`：通过本地假 webhook server 记录真实外发 payload
- 新黑盒流程分两段：
  - 先验证冷启动端口切换、alias、生效后的 `skipped_manual`
  - 再写入 prompt/channel 配置并重启，通过 warning/critical 两条 trace 分别验证 prompt 下发和 webhook 阈值过滤
- 黑盒里现在额外锁住了：
  - 只有 active prompt 的业务内容会进入最终 prompt 模板
  - `ai_language=zh` 会影响最终 prompt 模板
  - `threshold=critical` 时 warning 不发、critical 才发
  - 配置了 `secret` 的飞书 webhook 会自动补 `timestamp/sign`

# Verification

- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

结果：
- 黑盒脚本通过
- 已确认：
  - `prompt` 真从 Settings 进入 C++ 冷启动模板，再下发到 AI proxy
  - `active_prompt_id` 真决定最终使用哪条 prompt
  - `webhook channel` 的 `webhook_url / threshold / secret` 都被真实消费
  - `warning` 不告警，`critical` 真外发，且 payload 带飞书签名字段

# Learning Tips

## Newbie Tips

- Prompt 这种配置最容易写成“只存不吃”。如果你只是查 SQLite 或 `/settings/all`，你最多只能证明它保存了，不能证明模型调用时真的用了它。
- Webhook 这种配置也一样。只看 notifier 对象构造成功没有意义，真正值钱的是“warning 不发、critical 才发、发出去的 payload 长什么样”。

---

# Git Commit Message

fix(ai): 修复 glm 超时链路并透传 provider timeout

# Modification

- `server/ai/TraceProxyAi.cpp`
- `server/ai/proxy/schemas.py`
- `server/ai/proxy/main.py`
- `server/ai/proxy/providers/base.py`
- `server/ai/proxy/providers/gemini.py`
- `server/ai/proxy/providers/glm.py`
- `server/ai/proxy/providers/mock.py`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `server/tests/manual_glm_trace_probe.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 在 C++ `TraceProxyAi` 请求体里补入 `timeout_ms`，把外层 caller 的总等待预算继续透传给 Python proxy。
- 在 Python `TraceAnalyzeRequest` 中新增 `timeout_ms` 字段，并在 `/analyze/trace/{provider}` 路由里继续往 provider 透传。
- 为 `AIProvider` 的 `analyze_trace` 统一补 `timeout_ms` 签名；`mock/gemini` 当前先占位透传，避免参数不匹配打断测试链路。
- 在 `GlmProvider` 中增加 `_resolve_upstream_timeout_seconds()`，把上游 `httpx` timeout 固定裁成“比外层 caller 早 1 秒”，避免外层和内层同一时刻一起超时。
- 更新手工联调脚本 `manual_glm_trace_probe.py`：
  - `--timeout-sec` 负责本地等待 proxy 多久
  - `--provider-timeout-sec` 负责传给 GLM provider 的上游预算
  这样就能区分“proxy 自己没回”和“proxy 已经拿到了上游 TIMEOUT 并正常回给你”。
- 新增两条 Python 测试：
  - 锁定 `timeout_ms` 从 Trace 路由请求透传到 provider
  - 锁定 GLM 上游 `httpx` timeout 在 `timeout_ms=30000` 时会裁成 `29.0s`

# Verification

- `source /home/llt/Project/llt/venv/bin/activate && python3 -m unittest server/tests/ai_proxy_trace_protocol_test.py`
- `cmake --build server/build --target LogSentinel`
- `source /home/llt/Project/llt/venv/bin/activate && python3 server/tests/manual_glm_trace_probe.py probe-proxy --help`
- `git diff --check`

结果：

- `ai_proxy_trace_protocol_test.py`：`7/7` 通过
- `LogSentinel`：重编通过
- `manual_glm_trace_probe.py probe-proxy --help`：已确认新的双超时参数可见
- `git diff --check`：通过

# Learning Tips

## Newbie Tips

- “把 timeout 从 10s 改成 30s”不等于真正修好超时链路。只要内层 provider 和外层 caller 用同一个截止时间，还是会出现外层先报 `HTTP 0`、拿不到 proxy 结构化错误 body 的问题。
- 调试多层网络链路时，要把“本地等待多久”和“上游 provider 允许等多久”拆开看。前者是调试者愿意等多久，后者是 proxy 内部什么时候判定上游失败，这两件事不是一个变量。

## Function Explanation

- `timeout_ms`：这次 Trace AI 请求的总等待预算，先由 C++ 外层持有，再透传给 Python proxy。
- `_resolve_upstream_timeout_seconds()`：把调用方总预算裁成 GLM 上游 HTTP timeout 的函数。当前规则是固定留 `1s` 提前量，让 proxy 有时间把 TIMEOUT 变成统一 JSON。
- `call_provider_in_threadpool(...)`：路由层把同步 provider 扔到线程池的桥。测试里不要把红灯绑到它的线程调度细节上，直接 monkeypatch 抓 kwargs 更稳。

## Pitfalls

- 只改 C++ `TraceProxyAi` timeout 没用。因为真正卡住的是 Python proxy 去调用 GLM 上游的那层 HTTP。
- 只改 Python provider timeout 也没用。因为如果 C++ 不把预算传下来，provider 只能继续用固定值，还是会和外层 timeout 撞车。

---

# Git Commit Message

test(settings): 补双 provider 黑盒并修正 no-auto-start-proxy 语义

# Modification

- `server/tests/smoke_settings_blackbox.py`
- `server/src/main.cpp`
- `CurrentTask.md`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 扩展 `smoke_settings_blackbox.py` 里的本地探针服务，让同一个 fake proxy 可以同时模拟：
  - `/analyze/trace/gemini`
  - `/analyze/trace/glm`
  - 以及已有的 `/analyze/trace/mock` / `/webhook`
- 在黑盒主流程中新增 5 条双 provider 场景：
  - `gemini` 主路成功
  - `glm` 主路成功
  - `gemini -> glm` 自动降级成功
  - `glm -> gemini` 自动降级成功
  - 主备都失败时落 `failed_both`
- 为黑盒补了最小 SQLite 查询 helper，直接查 `trace_summary.ai_status / ai_error` 与 `trace_analysis` 条数，而不是只看探针有没有收到请求。
- 修复 `main.cpp` 里 `--no-auto-start-proxy` 的语义 bug：
  - 之前它不仅阻止自动拉起 sidecar，还会顺手把整个 Trace AI 主链关掉
  - 现在它只表示“不自动拉 sidecar”，不再阻止创建 `TraceProxyAi`
  - 这样像“后端打本地 fake proxy”的黑盒测试和后续单入口部署场景才能成立
- 同步更新 `CurrentTask.md`，把“双真实 provider + fallback 黑盒已收口”记入当前基线和验收标准

# Verification

- `cmake --build server/build --target LogSentinel`
- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

结果：

- `LogSentinel` 重编通过
- `smoke_settings_blackbox.py` 通过，已确认：
  - `gemini` 主路成功
  - `glm` 主路成功
  - `gemini -> glm` 自动降级成功
  - `glm -> gemini` 自动降级成功
  - 主备都失败时 `trace_summary.ai_status=failed_both`
- `git diff --check` 通过

# Learning Tips

## Newbie Tips

- `--no-auto-start-proxy` 这种名字很容易把两个语义搅在一起：
  - “不要自动拉 sidecar”
  - “整个 AI 主链禁用”
  这两个不是一回事。只要后端还能打一个已存在的 proxy 地址，AI 主链就应该照常工作。
- 黑盒测双 provider 时，不要把重点放在“收到了几个 HTTP 请求”。真正值钱的是最终落库状态：
  - 成功时是不是 `completed`
  - 双失败时是不是 `failed_both`
  - `ai_error` 有没有带上主备两边的失败信息

## Function Explanation

- `build_probe_success_payload / build_probe_failure_payload`：本地 fake proxy 用来统一构造成功/失败协议的 helper，专门模拟真实 Python proxy 对 C++ 的外部契约。
- `restart_server_with_fake_proxy(...)`：把“停旧进程 -> 等端口关闭 -> 用本地 fake proxy 重启后端”这套动作收进一个 helper，避免 5 条场景各自复制一遍重启逻辑。
- `query_trace_summary_row(...)`：黑盒里直接查 SQLite 主表当前最终状态的 helper，用来证明 worker 收尾后的真实结果，而不是只看内存或日志。

## Pitfalls

- 如果黑盒里继续传 `--trace-ai-provider mock` 这种 CLI override，就会把 Settings 里的 `ai_provider` 冲掉，最后测到的是 CLI，不是配置。
- 如果不修 `enable_trace_ai` 的条件，只要脚本带了 `--no-auto-start-proxy`，后端就会直接把 AI 主链关掉，表面上像是 provider/fallback 没生效，实际上是根本没构出来。

---

# Git Commit Message

feat(ai-proxy): 接入 GLM trace provider

# Modification

- `server/ai/proxy/providers/glm.py`
- `server/ai/proxy/main.py`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 新增 `GLM` Python provider，当前只先打通 Trace 主链 `analyze_trace`
- 使用智谱官方 `chat/completions` REST 接口，走：
  - `httpx.Client`
  - `response_format={"type":"json_object"}`
  - 本地 `json.loads + Pydantic(LogAnalysisResult)` 校验
- 成功路径会统一返回：
  - `ok`
  - `analysis`
  - `usage(input/output/total_tokens)`
- 失败路径会统一返回：
  - `ok=false`
  - `error_code`
  - `error_status`
  - `error_message`
- 在 `main.py` 注册 `glm` provider，并支持：
  - `GLM_API_KEY`
  - `BIGMODEL_API_KEY`
  - `GLM_MODEL`
- 为 `GLM` 补最小协议测试，锁定：
  - 请求体必须带 `response_format=json_object`
  - usage 提取与归一
  - 非法 JSON 不能伪造成成功 analysis

# Verification

- `python3 -m unittest server/tests/ai_proxy_trace_protocol_test.py`

结果：
- `5/5` 通过

# Learning Tips

## Newbie Tips

- 多 provider proxy 最怕的不是“第一次调不通”，而是每家 SDK/HTTP 语义都不一样，最后把重试、熔断、降级都拖脏。既然这里已经有统一 proxy，中间层优先自己控制请求体、超时和错误归一，后面会更稳。
- 智谱这条链当前正式支持的是 `json_object`，不是服务端强约束的 `json_schema`。所以你不能只靠模型说“我会返回 JSON”，还得自己在本地再做一次 `json.loads + schema` 校验。

## Function Explanation

- `httpx.Client`：Python 里的同步 HTTP 客户端。这里选它，不是因为它“更高级”，而是项目里已经有依赖，而且以后如果要统一代理、超时和连接复用，它比再混一套 `requests` 更顺。
- `LogAnalysisResult.model_validate(...)`：Pydantic v2 的模型校验入口。这里拿它做 provider 本地兜底，是为了把“字段缺失/风险等级非法”挡在 Python proxy，别把脏数据继续推给 C++。

## Pitfalls

- 不要把 `trace_text` 在 provider 里再拼一遍。Trace 路由上层已经把 `trace_context` 渲染进最终 prompt 了；如果下层再追加一次，同一份上下文就会重复输入模型。
- 不要把 “GLM provider 已注册” 等价成 “所有旧接口都支持”。这次只先接了 `analyze_trace`，其余抽象方法如果偷偷返回假值，会把路由语义搞脏，不如先明确 `NotImplementedError`。

---

# Git Commit Message

test(ai-proxy): 补 GLM 手工联调脚本

# Modification

- `server/tests/manual_glm_trace_probe.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 新增 `server/tests/manual_glm_trace_probe.py`，只提供两种最小动作：
  - `probe-proxy`：直打 `Python proxy /analyze/trace/glm`
  - `send-spans`：往后端发送一条 demo trace
- 脚本参数允许你自己选择：
  - `api_key`
  - `model`
  - `proxy/server` 地址
- 明确区分了两类联调语义：
  - `probe-proxy` 只验证 `GLM` 模型调用本身
  - `send-spans` 只验证 Trace 主链送数，不假装自动切后端 provider
- 已补可执行位，后续可直接 `./server/tests/manual_glm_trace_probe.py ...`

# Verification

- `python3 server/tests/manual_glm_trace_probe.py --help`
- `python3 server/tests/manual_glm_trace_probe.py probe-proxy --help`
- `python3 server/tests/manual_glm_trace_probe.py send-spans --help`

结果：
- 参数解析和帮助输出正常

# Learning Tips

## Newbie Tips

- “模型调用通不通”和“后端端到端有没有真的切到 glm”是两回事。前者只要直打 proxy 就能验，后者因为当前是冷启动配置，必须先改设置并重启后端。
- 手工联调脚本最怕职责过量。一个脚本里什么都自动做，看起来省事，实际排障最痛苦。这里故意拆成 `probe-proxy` 和 `send-spans` 两个动作，就是为了把根因边界切开。

## Function Explanation

- `argparse` 的 `subparsers`：把一个脚本拆成多个子命令。这里用它，是为了让 `probe-proxy` 和 `send-spans` 共用一个文件，但参数语义不互相污染。
- `requests.post(...).raise_for_status()`：把 HTTP 非 2xx 直接提升成异常。`send-spans` 用它，是因为这个动作只负责“后端有没有收下 span”，HTTP 层失败就该立刻停。

## Pitfalls

- 不能让 `send-spans` 偷偷去改后端设置。因为那样你会误以为“脚本一跑就代表后端已经切到 glm 了”，实际上当前 provider/model/api_key 还是冷启动语义。
- 不能在 `probe-proxy` 里复用后端 span JSON。proxy 真正吃的是“最终渲染好的 trace prompt”，不是 `/logs/spans` 的原始输入格式，把两者混成一套只会把测试边界搞脏。

---

# Git Commit Message

fix(ai): 补全 TraceProxyAi 传输层错误详情

# Modification

- `server/ai/TraceProxyTransportError.h`
- `server/ai/TraceProxyAi.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 新增 `TraceProxyTransportError.h`，统一构造 `TraceProxyAi` 的传输层报错文本
- 当 `cpr` 返回 `status_code=0` 或带有 `error.message` 时，错误文本现在会显式带出：
  - `url`
  - `cpr_error_code`
  - `cpr_error_message`
  - `Body`
- `TraceProxyAi.cpp` 不再继续抛空壳 `HTTP 0, Body:`，而是走统一的传输层错误格式
- 补了两条最小单测，锁住：
  - `HTTP 0` 时必须带出 cpr 连接错误详情
  - 普通非 200 响应仍然必须保留 body

# Verification

- `cmake --build server/build --target test_trace_session_manager_unit LogSentinel`
- `./server/build/test_trace_session_manager_unit --gtest_filter='TraceProxyTransportErrorTest.*'`

结果：
- 新增 2 条单测通过
- `LogSentinel` 重链通过

# Learning Tips

## Newbie Tips

- `HTTP 0` 通常不是“业务接口返回了 0”，而是 HTTP 客户端压根没拿到有效响应，常见原因是连不上、超时、TLS 握手失败。
- 调试链路问题时，最值钱的不是马上改业务逻辑，而是先把传输层错误显式打印出来。不然你面对的只是症状，不是根因。

## Function Explanation

- `cpr::Response.error`：`cpr` 对 libcurl 传输层错误的封装。这里真正能告诉你“是不是连不上 127.0.0.1:8001”“是不是超时”的，就是它的 `code` 和 `message`。
- `BuildTraceProxyTransportErrorMessage(...)`：这次新加的统一报错构造函数。目的不是抽象炫技，而是避免 `TraceProxyAi.cpp` 每次手拼错误字符串时继续漏掉关键上下文。

## Pitfalls

- 如果只看 `status_code`，你会把“连接失败”和“服务端返回 500”这两类完全不同的问题混成一类，后面越查越偏。
- 只补日志、不补单测，下次很容易又有人把 `HTTP 0` 简化回原来的空壳文本。这个回归点必须锁住。

---

# Git Commit Message

fix(settings): 接通 ai_timeout_ms 的后端冷启动消费

# Modification

- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/src/main.cpp`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 在 `AppConfig` 中正式加入 `ai_timeout_ms`，默认值设为 `30000`
- 在 `SqliteConfigRepository.cpp` 中补齐：
  - `ai_timeout_ms` 的 seed
  - `ai_timeout_ms` 的读取与投影
- 在 `main.cpp` 中把 Trace AI 超时改成：
  - `CLI > Settings > 默认值`
- 后端日志现在会打印真正生效的 `timeout_ms`
- 扩展 `smoke_settings_blackbox.py`：
  - 保存 `ai_timeout_ms=30000`
  - 重启后校验 `/settings/all` 回填
  - 校验启动日志包含 `timeout_ms=30000`

# Verification

- `cmake --build server/build --target LogSentinel test_trace_session_manager_unit`
- `./server/build/test_trace_session_manager_unit --gtest_filter='TraceProxyTransportErrorTest.*'`
- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

结果：
- 后端重链通过
- 传输层错误测试仍通过
- Settings 黑盒通过，确认 `ai_timeout_ms` 已被真实消费

# Learning Tips

## Newbie Tips

- “前端有字段”和“后端真消费了字段”完全是两回事。这个问题本质上就是典型的假配置：页面能改，数据库能存，但主程序启动时根本没用它。
- 对这类冷启动配置，最硬的证据往往不是查库，而是看启动日志和实际行为。因为真正的语义是“它有没有进入构造函数”，不是“它有没有存在于 SQLite”。

## Function Explanation

- `trace_ai_timeout_explicit`：这次加的 CLI 优先级标记。作用不是多此一举，而是保住命令行 override 的语义，不让 Settings 无意间把手工调试参数覆盖掉。
- `effective_trace_ai_timeout_ms`：最终生效值。这里统一收口，避免主路和 fallback 两边各自再写一套判断，后面很容易漂移。

## Pitfalls

- 如果只把默认值从 10s 改到 30s，但不把 Settings 消费链接上，这个 bug 只是“变得没那么容易复现”，没有真的修掉。
- 如果只查 `/settings/all` 回填，不看启动日志，你仍然无法证明 `TraceProxyAi` 构造时用的到底是不是 30000。

## Function Explanation

- `LocalProbeService`：这次黑盒里内嵌的本地探针服务，同时扮演“假 AI proxy”和“假 webhook server”。这样脚本自己就能拿到实收请求，不需要再依赖额外外部进程。
- `active_prompt_id`：`SystemConfig` 启动时优先按这个 id 取 prompt；如果找不到，才回退到第一个 `is_active=true` 的 prompt。

## Pitfalls

- 如果 prompt 黑盒只断言 `/settings/all` 回填正确，那本质上还是“存储测试”，不是“消费测试”。
- webhook 黑盒如果只发 critical，一样证明不了 threshold 过滤；必须先来一条 warning，确认它真的被挡住。

---

# Git Commit Message

test(settings): 补 worker threads 与 retention 第三层黑盒联调

# Modification

- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260412-refactor-settings-entry.md`

# What Changed

- 继续扩展 `server/tests/smoke_settings_blackbox.py`，在现有端口/alias/AI/prompt/webhook 黑盒基础上，再覆盖：
  - `kernel_worker_threads`
  - `log_retention_days`
- `kernel_worker_threads` 的验证方式：
  - 保存为 2
  - 重启后直接抓启动日志里的 `Thread Model: ... 2 worker threads`
  - 不再只看 `/settings/all` 回填，避免把“存进去”和“真建线程”混为一谈
- `log_retention_days` 的验证方式：
  - 在第三次启动前，直接向 SQLite 塞一条 2 天前的 trace 和一条当前 trace
  - 依赖启动清理把旧 trace 删掉、保留新 trace
  - 用数据库结果直接证明 retention 真执行了

# Verification

- `python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

结果：
- 黑盒脚本通过
- 已确认：
  - `kernel_worker_threads=2` 在重启后真的进入线程池创建日志
  - `log_retention_days=1` 会在启动清理时删掉过期 trace，但保留未过期 trace

# Learning Tips

## Newbie Tips

- 像 `kernel_worker_threads` 这种冷启动参数，真正有价值的证据往往不在配置接口回填，而在启动日志或实际线程行为里。
- retention 这种后台任务如果不先人工造旧数据，就只能证明“功能存在”，证明不了“过期判定和删除动作真的发生了”。

## Function Explanation

- `wait_process_log_contains(...)`：轮询进程输出直到出现目标文本，用来给“启动期一次性生效”的配置项留证据。
- `seed_retention_trace_rows(...)`：直接往 SQLite 塞一条过期 trace 和一条新 trace，让 retention 测试能避开前链路干扰，专门验证清理语义。

## Pitfalls

- 只查 `/settings/all` 的 `kernel_worker_threads=2` 不足以证明真生效，因为这只能说明数据库里是 2，不能说明线程池真按 2 条 worker 建起来。
- retention 如果在服务运行中再插旧数据，启动清理这条语义就被你自己绕开了；测试启动清理，就必须在启动前把旧数据准备好。
