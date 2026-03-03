## 记录：TraceSessionManager 单测占位骨架接入 CTest

## Git Commit Message
`test(trace): 新增TraceSessionManager占位单测并接入CTest`

## Modification
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 在需求还在迭代时，先落“可编译的占位测试”能把测试意图固定下来，后续补实现时不容易漏掉关键场景。
- 把测试目标接到 CTest 比只放一个文件更重要，因为只有接入后 CI 才会持续提醒我们补齐断言。

### Function Explanation
- `GTEST_SKIP()`：显式标记“该用例尚未实现”，测试报告里会显示 skipped，便于区分“失败”与“待完成”。
- `gtest_discover_tests(...)`：让 CTest 自动发现每个 `TEST`，后续可以按用例粒度筛选执行。
- `add_test(...)`：把测试二进制注册给 CTest，保证 `ctest` 能统一调度。

### Pitfalls
- 只创建测试文件但不改 CMake，会导致本地看得到代码、CI 却完全不执行。
- 占位测试如果没有明确命名到具体行为（比如触发条件、通知策略），后续容易演变成“写了但没真正测到核心风险”。
