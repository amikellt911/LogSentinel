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
