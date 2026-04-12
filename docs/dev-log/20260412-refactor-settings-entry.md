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
