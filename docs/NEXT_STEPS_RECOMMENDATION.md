# 下一步行动建议：构建设置处理层 (Settings Handler)

基于对现有代码库 (`server/`) 和项目文档 (`DEVELOPMENT_PLAN.md`, `BACKEND_IMPL_GUIDE.md`) 的分析，我强烈建议接下来的首要任务是**开发 `SettingsHandler` 并完成从前端到数据库的配置链路**。

## 1. 为什么先做这个？(Why)

1.  **链路打通**: 
    -   **底层 Ready**: `SqliteConfigRepository` 已经完成并经过了严格测试 (Unit Tests)。
    -   **前端 Ready**: 前端已经有配置页面。
    -   **缺失环节**: 目前独缺中间的 HTTP 接口层 (`Handler`)。只要补上这一环，用户就可以真正的在界面上修改并保存配置（如 AI Key, 报警渠道等）。
2.  **核心体验**: 配置持久化是工具“可用”的基石。没有它，用户每次重启都要重新配置，体验极差。
3.  **开发顺畅**: 这个任务依赖关系清晰（只依赖刚写好的 Repo），没有复杂的多线程或算法逻辑，适合作为下一个里程碑。

## 2. 具体执行计划 (Action Plan)

建议将任务拆解为以下子任务：

### 2.1 创建 `ConfigHandler` 类
在 `server/handlers/` 目录下创建 `ConfigHandler.h` 和 `ConfigHandler.cpp`。

*   **职责**:
    *   接收 HTTP 请求 (GET/POST)。
    *   解析 JSON Body (利用 `nlohmann/json`)。
    *   调用 `SqliteConfigRepository` 的方法。
    *   返回 JSON 响应。

*   **核心方法**:
    *   `handleGetSettings(req, resp, conn)`: 对应 `GET /api/settings/all`。调用 Repo 的 `getAppConfig`, `getAllPrompts`, `getAllChannels`，组装成 `AllSettings` 结构体（已在 `ConfigTypes.h` 定义）并返回。
    *   `handleUpdateAppConfig(req, resp, conn)`: 对应 `POST /api/settings/config`。解析前端发来的 KV 数组或对象，调用 Repo 的 `handleUpdateAppConfig`。
    *   `handleUpdatePrompts(req, resp, conn)`: 对应 `POST /api/settings/prompts`。解析 Prompt 数组，调用 Repo 的 `handleUpdatePrompt`。
    *   `handleUpdateChannels(req, resp, conn)`: 对应 `POST /api/settings/channels`。解析 Channel 数组，调用 Repo 的 `handleUpdateChannel`。

### 2.2 注册路由 (Router Registration)
在 `server/src/main.cpp` (或初始化路由的地方) 中，将上述 Handler 绑定到 URL 路径。

*   **路径规划**:
    *   `GET  /api/settings/all`
    *   `POST /api/settings/config`
    *   `POST /api/settings/prompts`
    *   `POST /api/settings/channels`

### 2.3 集成测试
*   使用 `curl` 或 Postman 模拟前端请求，验证数据能否正确写入数据库并查回。
*   验证“热更新”机制：修改配置后，是否需要重启？(注：Repo 已经实现了内存缓存更新，理论上是热更新的，除了像端口号这种硬指标)。

## 3. 注意事项与坑 (Tips & Pitfalls)

1.  **JSON 解析容错**: 
    *   前端发来的 JSON 可能格式不对，或者字段缺失。务必使用 `try-catch` 包裹 `json::parse` 和后续的字段访问，防止 Handler 崩溃导致服务重启。
    *   返回 HTTP 400 (Bad Request) 当 JSON 解析失败时。

2.  **CORS (跨域)**:
    *   虽然 `Router.cpp` 里已经有简单的 `OPTIONS` 处理，但要确保 Handler 返回的 Header 里包含必要的 CORS 头（如果不是统一处理的话）。

3.  **依赖注入**:
    *   `ConfigHandler` 需要持有 `SqliteConfigRepository` 的指针或引用。需要在 `main.cpp` 里把 Repo 实例化好，传给 Handler。

4.  **线程安全**:
    *   Handler 是在 HTTP 线程池中运行的。`SqliteConfigRepository` 内部已经加了锁 (`shared_mutex`)，所以 Handler 层面**不需要**额外加锁，直接调用 Repo 方法即可。

## 4. 总结观点

**先搞 Handler！** 
它是连接“死数据”（数据库）和“活界面”（前端）的桥梁。搞定它，这个项目就从一个“演示 Demo”变成了一个真正能保存状态的“软件”。
