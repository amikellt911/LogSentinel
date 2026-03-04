# 20260304-test-test-assets

Git Commit Message:
test(tests): 软下线部分历史测试并更新测试台账

Modification:
- server/CMakeLists.txt
- server/tests/legacy/SqliteConfigRepository_test.cpp
- server/tests/legacy/SqliteLogRepository_test.cpp
- server/tests/legacy/util_traceidgenerate_test.cpp
- server/tests/legacy/LogBatcher_test.cpp
- server/tests/legacy/README.md
- docs/TEST_ASSET_LEDGER.md
- docs/todo-list/Todo_TestAssets.md

Learning Tips:
Newbie Tips:
- 软下线推荐“先迁移到 legacy + 退出默认 CTest/CI”，不要直接删除文件。这样回滚和对照都方便。
- CTest 的测试来源通常来自 `gtest_discover_tests`，只把文件移目录但不改 CMake，会导致构建报路径错误。

Function Explanation:
- `gtest_discover_tests(target)`：在 CMake 配置/构建阶段自动从 gtest 二进制中发现用例并注册给 CTest。
- `git mv`：在移动文件时保留 Git 历史关系，后续追踪 blame 更友好。

Pitfalls:
- 同仓库并发执行多个 `git` 命令可能触发 `.git/index.lock` 冲突；出现时先确认没有悬挂进程，再重试失败命令。
- 台账如果不和 CMake 同步，会出现“文档说下线了但 CI 还在跑”的认知不一致问题。
