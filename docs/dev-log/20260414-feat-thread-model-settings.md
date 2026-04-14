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
