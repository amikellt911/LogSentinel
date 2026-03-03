# Todo_TestAssets

- [x] 盘点 `server/tests` 下现有 C++/Python 测试文件
- [x] 盘点 CMake/CTest 接入情况与当前 CI workflow 覆盖范围
- [x] 输出测试资产台账文档，按“保留/优化/下线候选”分类
- [x] 在台账中补充每类资产的现状、问题、建议动作与优先级
- [x] 对下线候选脚本执行“软下线”标注（DEPRECATED 头注释 + 替代入口）
- [x] 将软下线脚本统一迁移到 `server/tests/legacy/`，降低主目录误用概率
- [x] 清理 CTest 重复注册（移除 `add_test` 与 `gtest_discover_tests` 并存）
- [x] 追加今日 dev-log，记录本次测试资产梳理结论
