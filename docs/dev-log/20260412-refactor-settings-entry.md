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
