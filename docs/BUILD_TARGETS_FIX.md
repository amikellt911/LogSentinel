# 关于构建目标变化的说明与修复 (Explanation of Build Targets)

你观察到的那些“奇奇怪怪的东西”（如 `curl-example-*` 等）是由于我们最近修改了构建配置，从使用系统安装的 `curl` 切换到了**从源码编译 `curl`**。

## 1. 为什么会出现这些目标？(Why)

*   **原因**: 为了解决 `libpsl` 和 `meson` 的依赖问题，我们将 `server/CMakeLists.txt` 中的 `CPR_USE_SYSTEM_CURL` 设置为了 `OFF`。
*   **后果**: 这意味着 CMake 会下载 `curl` 的源码并作为子项目编译。`curl` 默认的构建配置会生成所有的示例程序 (`curl-example-*`)、命令行工具 (`curl-exe`) 和它自己的测试用例。
*   **影响**: 这些额外的目标污染了你的 IDE 构建列表，使得选择 `LogSentinel` 变得困难。

## 2. 如何去掉它们？(How to Fix)

我已经修改了 `server/CMakeLists.txt`，在引入 `cpr` 之前显式关闭了 `curl` 的这些选项：

```cmake
# 新增的配置
set(BUILD_CURL_EXE OFF CACHE INTERNAL "Disable curl executable")
set(BUILD_EXAMPLES OFF CACHE INTERNAL "Disable examples")
set(CURL_HIDDEN_SYMBOLS ON CACHE INTERNAL "Hide symbols")

# 原有的 FetchContent ...
```

### 3. 你需要做什么？(Action Required)

为了让这些更改生效并清理旧的缓存，建议你执行一次**干净的重新构建**：

1.  **清理旧的构建文件夹**:
    删除 `server/build` 目录（或者在 IDE 中执行 "Clean Reconfigure"）。
2.  **重新生成 CMake**:
    ```bash
    cd server
    mkdir build && cd build
    cmake ..
    ```
3.  **检查**:
    现在你应该只能看到项目自己的目标（如 `LogSentinel`, `test_config_repo` 等），那些 `curl-example-*` 应该已经消失了。

这个修改不仅让 IDE 列表更清爽，还能加快一点编译速度，因为不用去编译那几十个示例程序了。
