# LogSentinel - MVP2 迭代计划 (TodoList)

## 目标：从 MVP 到“准生产级”系统
本阶段的目标是补完 MVP 缺失的关键功能，增强系统的健壮性，并进行初步的架构优化，使 LogSentinel 从“技术验证品”转变为“有实用价值的工具”。

## 第一优先级：核心功能深化 (可用性)
让系统真正具备业务价值，能够连接真实 AI 并发出告警。

- [ ] **1. 集成真实 AI 服务**
    - [ ] **定义接口**：创建 `core/IAnalyzer.h`，定义 `virtual std::string analyze(const std::string& log) = 0;`。
    - [ ] **实现客户端**：创建 `ai/RealAIClient` (或复用/改造现有的 `GeminiApiAi`)。
        - [ ] 实现 `IAnalyzer` 接口。
        - [ ] 使用 `cpr` (当前项目已引入) 或 `cpp-httplib` 发起异步 HTTP 请求。
        - [ ] 处理 API Key 配置、请求构建与响应解析。
    - [ ] **配置开关**：在 `main.cpp` 中增加配置逻辑（命令行参数或配置文件），决定实例化 `MockAIAnalyzer` 还是 `RealAIClient`。

- [ ] **2. 实现 Webhook 告警**
    - [ ] **创建通知器**：实现 `util/WebhookNotifier` 类。
        - [ ] 提供 `notify(const std::string& payload)` 方法。
        - [ ] 使用 HTTP 客户端向预配置 URL 发送 POST 请求。
    - [ ] **集成到 Worker**：改造 `src/` 下的业务处理逻辑（Lambda 或 Task）。
        - [ ] 解析 AI 分析结果 JSON。
        - [ ] 若 `severity == "high"`，异步调用 `WebhookNotifier::notify`。
        - [ ] 确保告警发送不阻塞主业务流程（可考虑独立的告警线程或回调）。

## 第二优先级：系统健壮性增强 (可靠性)
引入“保险丝”机制，防止系统在异常和高负载下崩溃。

- [ ] **1. 实现背压机制 (Back-Pressure)**
    - [ ] **有界队列**：修改 `ThreadPool`，为任务队列 `tasks_` 设置上限（如 100,000）。
    - [ ] **拒绝策略**：修改 `ThreadPool::submit()`，当队列满时立即返回 `false`。
    - [ ] **验证测试**：使用 `wrk` 进行压测，验证在高负载下服务器返回 `503 Service Unavailable`，且内存占用稳定。

- [ ] **2. 引入 AI 调用稳定性组件**
    - [ ] **并发控制**：限制同时发往 AI API 的请求数量（如最多 4 个并发）。
        - *实现建议*：使用 `std::counting_semaphore` (C++20) 或原子计数器。
    - [ ] **重试与退避**：为 `RealAIClient` 增加重试逻辑。
        - [ ] 最大重试次数（如 3 次）。
        - [ ] 指数退避策略（500ms -> 1s -> 2s）。
    - [ ] **熔断器 (Circuit Breaker)** (高级项)：
        - [ ] 实现状态机：CLOSED -> OPEN -> HALF_OPEN。
        - [ ] 连续失败 N 次后熔断，快速失败，保护外部服务和自身资源。

## 第三优先级：架构与性能优化 (可维护性与效率)
偿还技术债务，为未来扩展打下基础。

- [ ] **1. 架构重构**
    - [ ] **路由封装**：实现 `http/Router` 类，替换 `main.cpp` 中的 `if/else` 硬编码路由。
    - [ ] **上下文封装**：实现 `http/RequestContext`，解耦传输层 (`TcpConnection`) 与应用层逻辑。
    - [ ] **任务原子化** (高级项)：引入 `Orchestrator` 和 `ITask` 接口，将大 Lambda 拆分为 `PersistRawLogTask`, `AiAnalysisTask`, `PersistResultTask` 等独立步骤。

- [ ] **2. 性能分析与优化**
    - [ ] **性能剖析**：使用 `gprof` 或 `perf` 分析高负载下的 CPU 热点。
    - [ ] **批量处理 (Batching)**：
        - [ ] **Worker 改造**：一次从队列取出 N 个任务（如 100 个）。
        - [ ] **数据库优化**：改造 `SqliteLogRepository::SaveRawLog` 支持 `vector<Task>`，使用 SQLite 事务 (Transaction) 进行批量插入。

---
*注：建议按优先级顺序执行。完成第一优先级后系统功能基本完整；完成第二优先级后系统具备生产级稳定性；第三优先级则是毕业设计论文中“系统优化”章节的核心素材。*
