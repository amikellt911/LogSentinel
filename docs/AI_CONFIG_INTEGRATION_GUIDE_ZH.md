# 后端 AI 配置集成与 LogHandler 改造文档

## 1. 修改概览

为了实现后端动态读取 AI 配置（API Key, Model, Prompt）并传递给 AI Provider，我们对以下模块进行了改造：

1.  **AnalysisTask (`server/core/AnalysisTask.h`)**:
    *   新增了 `ai_api_key`, `ai_model`, `prompt` 字段，用于在任务流中携带配置信息。
2.  **AiProvider (`server/ai/AiProvider.h`)**:
    *   更新了 `analyzeBatch` 和 `summarize` 接口，增加了 `api_key`, `model`, `prompt` 参数。
    *   这意味着 AI 的具体实现（如 `GeminiApiAi`, `MockAI`）不再依赖启动时的静态配置，而是可以处理请求级别的动态配置。
3.  **GeminiApiAi (`server/ai/GeminiApiAi.cpp`)**:
    *   实现了新的接口，将接收到的 `api_key` 等参数注入到发送给 Python Proxy 的 JSON 请求体中。
4.  **LogHandler (`server/handlers/LogHandler.cpp`)**:
    *   **依赖注入**: 构造函数新增 `SqliteConfigRepository` 依赖。
    *   **逻辑变更**: 在 `handlePost` 中，从 `config_repo` 读取 `AppConfig` 和 `Active Prompt`，填充到 `AnalysisTask` 中，然后推送到 `Batcher`。
5.  **LogBatcher (`server/core/LogBatcher.cpp`)**:
    *   **逻辑变更**: 在 `processBatch` 中，从批次的第一条任务中提取 AI 配置（假设同一批次的配置相同），并传递给 `ai_client_->analyzeBatch`。
6.  **main.cpp**:
    *   更新了 `LogHandler` 的实例化代码，传入了 `config_repo`。
7.  **CMakeLists.txt**:
    *   解决了 `cpr` 库在没有 `meson` 环境下构建 `libpsl` 失败的问题，通过强制关闭 `libpsl` 使用。

## 2. 新手可能不知道的点 (Tips)

*   **依赖注入 (Dependency Injection)**:
    *   我们在 `main.cpp` 中创建 `config_repo`，然后传给 `LogHandler`。这样做的好处是 `LogHandler` 不需要知道如何创建 `config_repo`，也不强依赖具体的实现（虽然目前是硬编码类型）。如果未来想换成 `RedisConfigRepository`，只需要改 `main.cpp` 和 `LogHandler` 的接口定义即可，方便测试和解耦。
*   **数据移动 (std::move)**:
    *   在 `LogBatcher::push` 和 `LogHandler` 中，我们大量使用了 `std::move`。例如 `batcher_->push(std::move(task))`。这避免了 `AnalysisTask`（其中包含大字符串）的深拷贝，提高了性能。新手容易忘记 move 而导致不必要的拷贝。
*   **Batcher 的配置策略**:
    *   你可能会问：如果一批任务里，前5个用 Key A，后5个用 Key B，怎么办？
    *   目前的实现是取 `batch[0]` 的配置。对于大多数场景（全局配置修改），这是可接受的。如果配置频繁突变，可能导致同一批次内的后续任务使用了“旧任务”的配置（或者反之）。但在日志分析场景下，配置通常是全局稳定的。如果要完美解决，需要按配置分组 batch，但这会大大增加复杂性。

## 3. 关键函数说明

*   **`nlohmann::json::parse` 与 `dump`**:
    *   在 `GeminiApiAi.cpp` 中，我们频繁使用这两个函数。`parse` 把字符串变对象，`dump` 把对象变字符串。注意处理 `try-catch`，因为网络返回的可能不是合法的 JSON。
*   **`cpr::Session`**:
    *   我们使用了 `cpr` 库发 HTTP 请求。注意设置 `SetTimeout`，防止网络卡死导致 worker 线程耗尽。

## 4. 避坑指南 (Pitfalls Avoided)

*   **CMake 构建陷阱**:
    *   `cpr` 库默认会尝试构建 `libpsl`，这需要 `meson` 构建系统。在没有 `meson` 的环境中（如某些容器或简化环境），这会导致构建失败。
    *   **解决**: 我们在 `CMakeLists.txt` 中显式设置了 `CPR_USE_SYSTEM_LIB_PSL ON` (为了跳过 cpr 内部的 include) 和 `CURL_USE_LIBPSL OFF` (为了让 curl 不去寻找它)。这是一个“欺骗” CMake 的技巧。
*   **接口变更的连锁反应**:
    *   修改 `AiProvider` 的虚函数接口后，必须同时修改所有子类 (`MockAI`, `GeminiApiAi`)，否则编译会报错（因为它们变成了抽象类）。同时，单元测试中的 Mock 对象也可能需要更新。
*   **Prompt 获取逻辑**:
    *   在 `LogHandler` 中获取 Prompt 时，我们不仅检查了 `active_prompt_id`，还做了一个 fallback：如果找不到 ID 对应的，就找第一个 `is_active=true` 的。这防止了用户删除了当前 Active 的 Prompt 后导致系统取不到 Prompt 的问题。
