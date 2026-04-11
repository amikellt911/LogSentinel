Git Commit Message
fix(db): 收口 SQLite 默认路径解析并修复删错库问题

Modification
- server/util/DbPathResolver.h
- server/util/DbPathResolver.cpp
- server/tests/DbPathResolver_test.cpp
- server/CMakeLists.txt
- server/src/main.cpp
- server/persistence/SqliteConfigRepository.cpp
- server/persistence/SqliteTraceRepository.cpp
- docs/todo-list/Todo_ConfigRepository.md

Learning Tips

Newbie Tips
相对路径默认永远是按进程当前工作目录解释，不是按“源码目录”也不是按“可执行文件目录”解释。你在不同目录下启动同一个二进制，只要代码里写的是相对路径，实际打开的文件就可能完全不是同一份。

Newbie Tips
`CREATE TABLE IF NOT EXISTS` 只能保证“表不存在时创建表”，不会帮你给旧表自动补列。所以旧库 schema 落后于新代码时，最常见的炸法就是 `no such column`。

Function Explanation
`std::filesystem::read_symlink("/proc/self/exe")` 在 Linux 下可以拿到当前真实可执行文件路径。这里比 `argv[0]` 更稳，因为 `argv[0]` 可能是相对路径、软链接，甚至只是一个命令名。

Function Explanation
`std::filesystem::weakly_canonical` 会把“已经存在的前缀路径”规整掉，再把不存在的尾部路径拼回去。它适合这种“目标文件可能还没创建，但仍然想先得到一份稳定绝对路径”的场景。

Pitfalls
如果把“默认路径策略”藏在 Repository 构造函数里，调用方根本看不出来最终用的是哪份 DB。结果就是用户以为删了默认库，实际进程还在读另一份旧库，排障时非常恶心。

Pitfalls
路径策略应该只在一个地方决定。当前这刀把“默认路径怎么解析”收回到 `main.cpp`，Repository 只负责打开已经解析好的最终路径，这样职责才清楚。

本次补的中文注释
- `server/util/DbPathResolver.h`
  - 说明为什么默认库路径必须以可执行文件目录为锚点
  - 说明为什么显式 `--db` 不能再被内部偷偷改写
  - 说明为什么优先从 `/proc/self/exe` 解析真实可执行文件路径
- `server/util/DbPathResolver.cpp`
  - 说明默认路径为什么要固定到 `../persistence/data`
  - 说明显式相对路径为什么按当前工作目录解释
  - 说明 `/proc/self/exe` 失败时的兜底分支意义
- `server/tests/DbPathResolver_test.cpp`
  - 说明为什么要锁死默认路径和显式路径两种不同语义
- `server/src/main.cpp`
  - 说明为什么启动时必须直接打印最终 DB 路径
- `server/persistence/SqliteConfigRepository.cpp`
  - 说明为什么配置仓储不再内部决定默认 DB 路径
- `server/persistence/SqliteTraceRepository.cpp`
  - 说明为什么 Trace 仓储也要和配置仓储保持同一条路径边界

Verification
- `cmake --build server/build --target test_db_path_resolver`
- `./server/build/test_db_path_resolver`
- `cmake --build server/build --target LogSentinel`
- `timeout 5s ./server/build/LogSentinel --no-auto-start-proxy --db /tmp/logsentinel_dbpath_fix.db --port 18080`
- `timeout 5s ./server/build/LogSentinel --no-auto-start-proxy --port 18081`

Verification Result
- `test_db_path_resolver` 3/3 通过
- `LogSentinel` 编译通过
- 显式 `--db` 启动时打印并使用 `/tmp/logsentinel_dbpath_fix.db`
- 默认启动时打印并使用 `/home/llt/Project/llt/LogSentinel/server/persistence/data/LogSentinel.db`

---

Git Commit Message
refactor(frontend): 移除旧壳层模拟模式与运行待机假开关

Modification
- client/src/layout/MainLayout.vue
- client/src/stores/system.ts
- client/src/views/Dashboard.vue
- client/src/views/LiveLogs.vue
- client/src/views/History.vue
- client/src/i18n.ts
- docs/todo-list/Todo_Settings_MVP5.md

Learning Tips

Newbie Tips
如果一个前端开关根本不对应真实后端控制面，那它不是“演示增强”，而是误导。用户一旦把“页面上能点”理解成“系统真的会这么运行”，后面的联调判断会全乱。

Newbie Tips
删 UI 开关时，不能只删模板。还要顺着状态流看 store、watch、轮询、mock fallback 有没有一起清掉，否则只是把按钮藏起来，假逻辑还在背后跑。

Function Explanation
这次 `system.ts` 里保留了自动轮询，但删掉了 `toggleSystem / isRunning / isSimulationMode`。意思不是“前端永远知道后端正常”，而是“前端默认持续尝试真实请求，成不成功由接口结果决定，不再靠本地假状态决定”。

Pitfalls
`History.vue` 这种页面最容易藏“拉不到后端就临时拼一页 mock 数据”的分支。这个分支开发期看着省事，但一到联调阶段就会把真正的后端问题盖住。

本次补的中文注释
- `client/src/layout/MainLayout.vue`
  - 说明为什么要移除“模拟模式 / 系统待机-运行中”假控制面
- `client/src/stores/system.ts`
  - 说明为什么历史/日志读侧不能再偷偷塞 mock
  - 说明为什么 store 创建后默认持续轮询真实后端
- `client/src/views/LiveLogs.vue`
  - 说明为什么进入页面就直接开始真实日志轮询
- `client/src/views/History.vue`
  - 说明为什么历史页拉取失败时只能保持空态/错误态，不能回退 mock

Verification
- `npm run build`

Verification Result
- 构建仍未通过，但当前剩余报错都来自旧页面/旧组件的历史 TypeScript 问题，与这次删除假开关无关：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/views/AIEngine.vue`
  - `src/views/Settings.vue`
