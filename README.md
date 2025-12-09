# LogSentinel 项目

LogSentinel 是一个基于 C++ 实现的高性能日志分析服务。

项目采用 Reactor 模式处理网络I/O，并通过独立的线程池执行业务逻辑，以实现高并发和低延迟。
## 项目简介 (Introduction)

**LogSentinel** 是一个高性能、低延迟的日志分析引擎。它旨在解决传统日志系统“只存储、不理解”的痛点。

通过结合 **C++ 高并发网络架构**（Reactor 模式 + 线程池）与 **大语言模型（LLM）** 的推理能力，LogSentinel 能够实时接收海量日志，异步进行根因分析（Root Cause Analysis），并提供结构化的修复建议与实时 Webhook 告警。

## 系统演示 (Demo)

### 1. Web 监控面板
![Alt text](%E6%97%A0%E6%A0%87%E9%A2%98.png)

### 2. 系统架构
![Alt text](%E6%97%A0%E6%A0%87%E9%A2%98-1.png)

![Alt text](%E6%97%A0%E6%A0%87%E9%A2%98-2.png)

## 🛠️ 技术栈 (Tech Stack)

*   **Core Server**: C++17, CMake
*   **Network**: MiniMuduo (Epoll, Non-blocking I/O)
*   **Concurrency**: Thread Pool
*   **Persistence**: SQLite3 (WAL Mode)
*   **Network Client**: cpr 
*   **JSON Processing**: nlohmann/json
*   **AI Service**: Python 3.10, FastAPI, Google GenAI SDK
*   **Frontend**: HTML5, CSS3, JavaScript (Fetch API)

## 🔌 API 文档 (API Reference)

### 提交日志
*   **URL**: `POST /logs`
*   **Content-Type**: `application/json`
*   **Body**: `{"msg": "Error content..."}`
*   **Response**: `202 Accepted`, `{"trace_id": "..."}`

### 查询结果
*   **URL**: `GET /results/{trace_id}`
*   **Response**:
    *   `200 OK`: `{"result": {...}, "trace_id": "..."}`
    *   `404 Not Found`: 处理中或不存在

## 待优化项汇总 (Optimization Roadmap)

以下是根据各模块的 `README` 文件总结的未来优化方向列表。

### 1. 架构与核心逻辑 (Architecture & Core Logic)

- **HTTP层重构**:
    - 设计并使用 `RequestContext` 类来封装请求上下文，取代目前分散在 `httpCallback` 参数中的数据，使信息传递更集中、更可扩展。
- **增强系统韧性**:
    - 为 `SaveLog` 等关键的持久化操作增加 **重试或死信队列** 机制，当操作失败时（如数据库繁忙），能自动重试或将失败的日志存入死信队列，供后续分析处理。

### 2. 性能与并发 (Performance & Concurrency)

- **升级为 gRPC 通信**:
    - 在功能验证通过后，将服务间（如 C++ 与 Python 代理）的通信方式从 REST/HTTP 升级到 **gRPC**，以获得更高的性能和更强的类型安全。
- **线程池背压机制**:
    - 为线程池实现背压（Back-pressure）策略，如 **降级、熔断或直接拒绝请求**。这可以防止在极端高并发下，I/O线程（生产者）的生产速度远超工作线程（消费者）的处理速度，导致任务队列无限增长和内存耗尽。
- **数据库并发优化**:
    - 为每个工作线程 (`worker thread`) 分配一个 **线程局部 (`thread_local`) 的SQLite句柄**。这样每个线程可以独立操作数据库，从而使用 `SQLITE_OPEN_NOMUTEX` 模式来避免线程间的锁竞争，大幅提升写入性能。
- **处理数据库繁忙**:
    - 在代码中专门处理 `SQLITE_BUSY` 返回码，而不是直接抛出异常。可以实现一个带有退避策略的重试循环，以应对暂时的数据库锁定。

### 3. 数据库与持久化 (Database & Persistence)

- **时间戳精度**:
    - 将数据库中记录日志时间的 `received_at` 字段类型从 `TIMESTAMP` 改为 **整数类型的Unix时间戳**。在高负载下，任务从接收到执行之间可能存在延迟，使用时间戳能更精确地记录事件发生的时间。

### 4. API与外部集成 (API & External Integration)

- **统一API管理**:
    - 针对 `ai` 模块，建立一个统一的API管理层，用于适配和处理来自不同AI厂商的、风格各异的 RESTful API。

### 5. 可观测性 (Observability)

- **Trace ID 优化**:
    - 研究通过额外一轮哈希或其他算法来缩短 `trace_id` 的长度，使其在日志和监控系统中更易于阅读、存储和索引。

### 6. 测试环境 (Testing)

- **独立的压测客户端**:
    - 使用独立的机器（例如，阿里云的2核2G服务器）作为`wrk`压测的客户端，而不是在服务器本机上同时运行客户端和服务端。这可以避免因资源竞争（特别是CPU）导致测试结果不准确。

### 7. AI上下文管理策略 (AI Context Management Strategy)

为了解决大语言模型（LLM）的上下文窗口（Context Window）限制问题，特别是在处理长日志和多轮对话时，我们计划采用分阶段的演进策略来管理对话历史。

- **第一阶段：基本安全策略 (截断/滑动窗口)**
    - 保证程序在任何情况下都不会因为Token超限而崩溃。
- **第二阶段：AI辅助的上下文摘要**
    - 当对话历史达到一定长度时，触发AI将历史压缩成摘要。
- **第三阶段：向量数据库与RAG (检索增强生成)**
    - 将对话历史向量化存储，通过相似度搜索实现长期记忆。

### 8. 日志解析与模版提取 (Log Parsing & Template Extraction)

引入学术界 LogBatcher 的核心思想，在日志进入 AI 分析流程前增加一个“解析/模版提取”层。

- **模版分离与结构化**: 利用聚类算法或正则匹配，将日志分离为 模版 (Template) 和 参数 (Variables)。
- **缓存优先机制 (Caching First)**: 构建本地缓存存储已识别的模版正则，命中缓存则不调用 LLM。
- **微批处理 (Micro-batching)**: 对于未命中的日志，打包发送给 LLM 进行模版学习。

### 9. 未来思考 (Future Considerations)

- **服务启动编排**: 考虑使用 `systemd` 或容器编排管理 C++ 与 Python 服务。
- **AI上下文维护位置**: 探讨上下文管理应在 C++ 端还是 Python 端维护。

---

### 10. AI 基础设施与高性能推理灵感 (AI Infra & Inference Optimization) [NEW]

结合 AI Infra 领域的高性能推理（Inference）技术，将 LogSentinel 视为一个专用的推理服务网关，进一步压榨 C++ 性能。

- **基于长度的分桶调度 (Length-based Bucketing)**:
    - **痛点解决**: 解决“队头阻塞” (Head-of-line blocking) 问题。避免一条超长日志的分析阻塞了后续所有短日志的处理。
    - **实现思路**: 在 Reactor 的任务分发层，建立多个优先队列（如 `Short`, `Medium`, `Long`）。调度器根据日志的 Token 预估长度，将其分发到不同的队列，并由不同的 Worker 线程或批次处理。

- **异构流水线 (Heterogeneous Pipelining)**:
    - **痛点解决**: 消除 CPU (预处理) 等待 GPU/API (推理) 的空闲时间。
    - **实现思路**: 将日志处理拆解为 `Pre-process` (脱敏、截断、正则提取) 和 `Inference` (LLM调用) 两个独立阶段。利用 C++ 的异步回调机制，当 Batch N 在进行网络 IO (等待 LLM 响应) 时，CPU 立即开始处理 Batch N+1 的正则和脱敏工作，实现资源的打满。

- **分级处理与投机采样 (Tiered Processing / Speculative Decoding)**:
    - **痛点解决**: 降低昂贵的 LLM 调用成本和延迟。
    - **实现思路**: 采用“漏斗式”处理。
        1.  **L1**: 极速正则/关键词匹配 (0ms, C++) -> 拦截已知错误。
        2.  **L2**: 小参数模型或统计模型 (10ms, Python Sidecar) -> 快速分类风险。
        3.  **L3**: 仅当 L2 判定为“高疑难”或“未知风险”时，才透传给 Gemini/OpenAI (1s+) 进行深度语义分析。