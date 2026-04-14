# 2026-04-14 feat-thread-model-settings

## Git Commit Message
`feat(settings): 补 MiniMuduo io 线程冷启动配置`

## Modification
- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/src/main.cpp`
- `client/src/views/SettingsPrototype.vue`
- `server/tests/smoke_settings_blackbox.py`
- `docs/todo-list/Todo_Settings_MVP5.md`

## What Changed
- 在 `AppConfig` 增加 `kernel_io_threads`，把线程模型正式拆成两类冷启动配置：
  - `kernel_io_threads`：MiniMuduo I/O 线程数
  - `kernel_worker_threads`：主工作线程池线程数
- 在 `SqliteConfigRepository` 增加 `kernel_io_threads` 的解析与默认 seed，并把 `kernel_worker_threads` 的描述改成“主工作线程池线程数”，避免继续误导。
- 在 `main.cpp` 改线程装配逻辑：启动时先从配置快照读取 `kernel_io_threads`，再按 `CPU - io_threads` 推默认 worker 数，并做最小兜底，防止出现 `0` 或负数 worker。
- 在 `SettingsPrototype.vue` 增加 “MiniMuduo I/O 线程数” 输入框，并把旧的“工作线程数”文案改成“主工作线程数”；保存、回填、dirty check、重启提示都同步接上。
- 在第三层黑盒里增加 `kernel_io_threads` 冷启动验证，要求重启后的启动日志出现 `2 I/O threads, 2 worker threads`，避免只验证 SQLite 里存了值、没验证后端真的用了它。

## 中文注释
- `server/persistence/ConfigTypes.h`
  - 在 `AppConfig` 的线程字段前补注释，解释为什么要拆开 I/O 线程和主 worker 线程池。
- `server/persistence/SqliteConfigRepository.cpp`
  - 在 `ApplyConfigValue` 的线程配置分支补注释，说明两个 key 对应的运行位置不同。
- `server/src/main.cpp`
  - 在线程模型启动装配处补注释，解释 `kernel_io_threads` 和 `kernel_worker_threads` 分别喂给谁，以及为什么默认 worker 数要做兜底。
- `client/src/views/SettingsPrototype.vue`
  - 在 `kernel` 本地状态处补注释，说明页面为什么必须把两个线程旋钮拆开显示。

## Verification
- `cmake --build server/build --target LogSentinel`
- `/home/llt/Project/llt/venv/bin/python3 server/tests/smoke_settings_blackbox.py`
- `git diff --check`

## Verification Notes
- `cd client && npm run build` 当前未通过，但报错集中在仓库里既有的 TS 红灯：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/views/AIEngine.vue`
  - `src/views/Settings.vue`
  这批错误不是本次 `kernel_io_threads` 改动引入的，所以这次没有顺手扩 scope 去清。

## Learning Tips
### Newbie Tips
- “线程数”如果不说清楚是哪类线程，最后一定会把实验和配置语义讲乱。收包线程和业务 worker 线程都能影响吞吐，但它们堵住的位置根本不是一回事。
- 冷启动配置不能只看 `/settings/all` 回填。配置值能从 SQLite 读出来，只能证明“存进去了”；要证明“真的生效了”，必须盯启动日志、真实监听端口或真实行为。

### Function Explanation
- `std::thread::hardware_concurrency()`：返回当前进程可见的 CPU 核数，用来给线程模型做默认值参考；它可能返回 `0`，所以不能直接拿来当可靠配置。
- `el-input-number`：前端数字输入控件；这次用它让 `kernel_io_threads` 和 `kernel_worker_threads` 都走同一套数值编辑交互，避免用户输字符串再靠后端兜底。

### Pitfalls
- 如果只新增 `kernel_io_threads`，但不把 `kernel_worker_threads` 的文案一起改正，页面上还是会继续误导人，以为那个旧字段是 I/O 线程。
- 如果默认 worker 数直接写 `CPU - io_threads` 却不做兜底，一旦用户把 I/O 线程调大到接近或超过可见核数，主 worker 池就可能掉到 `0` 或负数，启动语义直接脏掉。

## 追加记录：设置页线程文案收口

### Git Commit Message
`fix(settings): 收口线程配置前台文案`

### Modification
- `client/src/views/SettingsPrototype.vue`

### What Changed
- 把设置页里的 `MiniMuduo I/O 线程数` 改成 `服务器 I/O 线程数`。
- 保留内部配置 key `kernel_io_threads` 不变，只收前台展示文案，避免把用户配置语义和底层网络库实现细节绑死。

### 中文注释
- `client/src/views/SettingsPrototype.vue`
  - 在 `kernel` 本地状态的注释里补了一句，明确页面文案为什么不能直接暴露底层网络库名。

### Verification
- `git diff --check`

### Newbie Tips
- 前台配置文案应该描述“这个旋钮控制的系统职责”，不要把底层库名直接甩给用户。库可以换，职责语义不该跟着飘。

## 追加记录：AI Proxy 并发上限 CLI

### Git Commit Message
`feat(ai): 增加 proxy max-workers 并发上限`

### Modification
- `server/ai/proxy/main.py`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `docs/todo-list/Todo_Benchmark.md`

### What Changed
- 给 Python AI proxy 增加 `--max-workers` CLI 参数，默认值是 `128`。
- 启动时把 `--max-workers` 写进 `app.state.ai_proxy_max_workers`，并在 startup 阶段改 AnyIO 默认线程 limiter 的 `total_tokens`。
- 启动日志新增 `max_workers=...`，方便 benchmark 和答辩时直接从命令与日志确认代理层并发配置。
- 新增两条 Python 单测：
  - `parse_proxy_args` 能读出 `--max-workers`
  - `configure_default_thread_limiter` 能真的把 AnyIO 默认 limiter 改成目标值

### 中文注释
- `server/ai/proxy/main.py`
  - 在 `parse_proxy_args` 前补注释，说明为什么 proxy 并发上限要走 CLI，而不是优先藏进环境变量。
  - 在 `normalize_proxy_max_workers / configure_default_thread_limiter` 前补注释，说明这里控制的是本地 AI 代理层并发，不是厂商配额承诺。
  - 在 `__main__` 启动入口补注释，说明当前先只收单进程 + 线程 limiter 这一个实验变量，不同时引入 uvicorn 多进程。
- `server/tests/ai_proxy_trace_protocol_test.py`
  - 在两条新单测里补注释，说明锁定的是 CLI 口径和 AnyIO limiter 真联动，而不是日志假打印。

### Verification
- `/home/llt/Project/llt/venv/bin/python3 -m unittest server.tests.ai_proxy_trace_protocol_test.AiProxyTraceProtocolTest.test_parse_proxy_args_reads_max_workers_from_cli`
- `/home/llt/Project/llt/venv/bin/python3 -m unittest server.tests.ai_proxy_trace_protocol_test.AiProxyTraceProtocolTest.test_configure_default_thread_limiter_updates_anyio_capacity`
- `git diff --check`

### Verification Notes
- 整份 `server/tests/ai_proxy_trace_protocol_test.py` 目前不能作为这一刀的全量验证口径。
  复现证据是：在纯 `unittest + asyncio.run` 环境里，直接 `await run_in_threadpool(lambda: {...})` 也会挂住。
  所以这次只把新增的 `--max-workers` 两条单测作为有效验证，不顺手扩大 scope 去收旧的 `run_in_threadpool` 挂起问题。

### Newbie Tips
- `run_in_threadpool()` 不等于“有一个显式可见的 ThreadPoolExecutor 配置项”。在这套栈里，真正限制并发的是 AnyIO 的 `CapacityLimiter` token 数。
- benchmark 变量最好优先走 CLI。命令能直接写进脚本和论文，别人复现实验时不需要再猜你的 shell 环境里到底塞了什么变量。

## 追加记录：Trace 生命周期 profile 第一刀

### Git Commit Message
`feat(trace): 增加生命周期 profile 冷启动配置`

### Modification
- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/src/main.cpp`
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_TraceSessionManager.md`

### What Changed
- 在 `TraceSessionManager` 增加 `TraceLifecycleProfile` 枚举，正式收口两档语义：
  - `protected`：保留当前主语义，命中结束条件后先进入 `Sealed`，dispatch 成功后写 completed tombstone。
  - `minimal`：命中结束条件后直接进入 `ReadyToDispatch`，下一次 sweep 就摘走 dispatch，不再保留 sealed grace，也不写 completed tombstone。
- 在 `TraceSession` 生命周期状态里增加 `ReadyToDispatch`，把“正常收口等待 sweep”跟“失败回滚后的 ReadyRetryLater”拆开，避免继续复用一套 timeout 语义。
- 在 `PushLocked / ScheduleSessionNode / SealSessionLocked / AddCompletedTombstoneLocked` 接入 profile 分支：
  - `minimal` 仍然坚持走 `sweep -> dispatch queue -> dispatch worker` 主路径，不做 direct dispatch。
  - `minimal` 下处于 `ReadyToDispatch` 的 session 不再吸收新 span，只返回 `AcceptedDeferred`。
- 在 `AppConfig / SqliteConfigRepository / main.cpp` 接通 `trace_lifecycle_profile` 冷启动配置，默认值为 `protected`。
- 在启动阶段增加 profile 解析和校验，非法值会直接拒绝启动，避免把脏配置静默带进状态机。
- 在 `TraceSessionManager_unit_test` 新增两条用例并跑绿：
  - `MinimalProfileSkipsSealedGraceAndDispatchesOnNextSweep`
  - `MinimalProfileDoesNotKeepCompletedTombstoneAfterDispatch`

### 中文注释
- `server/core/TraceSessionManager.h`
  - 给 `ReadyToDispatch`、`TraceLifecycleProfile`、`ScheduleReadyNode`、`SealSessionLocked` 补注释，说明 `protected/minimal` 的状态流差异。
  - 更新 `lifecycle_state` 和 `completed_trace_tombstone_ticks_` 注释，避免还按旧三态去理解。
- `server/core/TraceSessionManager.cpp`
  - 在 `PushLocked` 的 ready 分支补注释，说明为什么 `ReadyToDispatch/ReadyRetryLater` 都不能继续并 span。
  - 在 `AddCompletedTombstoneLocked` 补注释，说明为什么 `minimal` 要故意跳过 tombstone。
  - 在 `ScheduleReadyNode / SealSessionLocked / RebuildTimeWheel` 补注释，说明 `minimal` 为什么仍走 sweep 主路径。
- `server/persistence/ConfigTypes.h`
  - 在 `trace_lifecycle_profile` 字段上补注释，说明它为什么必须保持冷启动。
- `server/persistence/SqliteConfigRepository.cpp`
  - 在 `ApplyConfigValue` 的 `trace_lifecycle_profile` 分支补注释，说明存储层只存值，语义校验留给启动期。
- `server/src/main.cpp`
  - 在 profile 解析处补注释，说明这条配置为什么不能做热切。

### Verification
- `cmake --build server/build --target test_trace_session_manager_unit LogSentinel -j2`
- `./server/build/test_trace_session_manager_unit`
- `git diff --check`

### Learning Tips
#### Newbie Tips
- `ReadyToDispatch` 和 `ReadyRetryLater` 看起来都像“ready”，但物理含义完全不同。前者是正常收口，后者是下游失败回滚。如果把它们混成一种状态，后面时间轮和 late span 语义一定会脏。
- 冷启动配置的判断标准不是“它改的是不是字符串”，而是“它会不会改状态机语义”。只要会影响会话生命周期，就不该在运行中半路切。

#### Function Explanation
- `ScheduleSessionNode(...)`：根据 session 当前生命周期，把会话挂到不同的时间轮语义上。现在它不只是 timeout/retry 两档，还多了一档 `ReadyToDispatch`。
- `AddCompletedTombstoneLocked(...)`：给刚完成的 trace 留一个短暂 TIME_WAIT，专门拦截晚到 span 复活旧 trace。这次 `minimal` profile 故意把这层保护关掉，用来做对照实验。

#### Pitfalls
- 如果 `minimal` 命中结束条件后直接同步 dispatch，而不是继续走 sweep 主路径，你测出来的就不只是“去掉 grace/tombstone”的差异，而是把整个调度入口都换了，实验结论会串味。
- 如果 `ReadyToDispatch` 还允许继续并 span，那么它就不是真正的“minimal 无 grace”，而只是换了个名字的 `Sealed`，测试口径会自相矛盾。
