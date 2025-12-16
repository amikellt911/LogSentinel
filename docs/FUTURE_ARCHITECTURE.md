# 未来架构规划：高性能并发模型 (Future Architecture)

本文档基于现有架构存在的性能瓶颈（特别是 AI 任务阻塞界面响应的问题），规划了未来的多线程模型与任务调度架构。

## 1. 现状与瓶颈分析

**当前架构:**
*   **硬件:** 4 核 CPU。
*   **线程模型:** 1 个 IO 线程 (MiniMuduo EventLoop) + 1 个通用线程池 (ThreadPool, 3 个线程)。
*   **问题:**
    1.  **资源争抢:** 所有的任务（日志解析、AI 分析、前端 `/dashboard` 查询）都提交到同一个线程池。
    2.  **队头阻塞 (Head-of-Line Blocking):** AI 分析任务通常耗时较长（网络请求 + 推理，可能 >500ms）。如果线程池的 3 个线程都在处理 AI 任务，此时前端发来的 `GET /dashboard` 请求只能在队列中等待，导致用户界面“卡死”。
    3.  **调度策略单一:** 无法区分“高优先级交互任务”和“低优先级后台任务”。

---

## 2. 核心改进方案：双线程池与异步解耦

为了解决上述问题，建议将架构演进为 **资源隔离的双线程池模型**，并引入 **异步消息队列**。

### 2.1 架构图解

```mermaid
graph TD
    Client[前端/日志源] -- HTTP请求 --> IO_Thread[IO线程 (EventLoop)]

    subgraph "任务分发 (Router/Handler)"
        IO_Thread -- 1. 快速请求 (/dashboard) --> Interactive_Queue[交互任务队列]
        IO_Thread -- 2. 日志提交 (/logs) --> Log_Buffer[日志缓冲队列]
    end

    subgraph "Interactive Pool (交互线程池)"
        Interactive_Queue -- 取任务 --> IP_Thread1[线程 1]
        IP_Thread1 -- 读库 --> SQLite
    end

    subgraph "Worker Pool (后台重任务池)"
        Log_Buffer -- 批量/异步取任务 --> WP_Thread1[线程 A]
        Log_Buffer --> WP_Thread2[线程 B]
        Log_Buffer --> WP_Thread3[线程 C (甚至更多)]

        WP_Thread1 -- AI请求 (耗时) --> External_AI[外部 AI 服务]
        WP_Thread1 -- 写库 --> SQLite
    end
```

### 2.2 线程池拆分策略

我们将线程池拆分为两个独立的池，互不干扰：

#### A. 交互线程池 (Interactive Pool)
*   **职责:** 专门处理前端的查询请求 (`GET /dashboard`, `/history`, `/settings`)。
*   **特点:** 任务计算量小，主要是查数据库，要求 **低延迟**。
*   **配置建议:**
    *   **线程数:** 1 个即可（对于 4 核机器）。
    *   **原因:** 这些任务处理极快，1 个线程每秒能处理成百上千个请求，足以应付前端 1秒/次 的轮询，且永远不会被 AI 任务阻塞。

#### B. 工作线程池 (Worker Pool)
*   **职责:** 处理日志解析、AI 分析、数据库写入。
*   **特点:** 任务耗时长，容忍高延迟，吞吐量优先。
*   **配置建议:**
    *   **线程数:** 建议 **4 - 8 个**（甚至更多）。
    *   **原因:** 这里的任务大部分是 **IO 密集型**（等待 AI API 响应）。当线程 A 在等 HTTP 响应时，它处于睡眠状态，不消耗 CPU。此时线程 B 可以利用 CPU 做 JSON 解析。因此，线程数可以 **超过** 物理核数，以提高并发度。

---

## 3. 异步处理流水线 (Asynchronous Pipeline)

除了拆分线程池，任务的处理流程也需要改变：

*   **当前 (同步):**
    `请求 -> LogHandler -> 线程池执行(AI+入库) -> 返回响应`
    *(缺点：如果线程池满，LogHandler 会在 `send` 时阻塞或拒绝)*

*   **未来 (异步):**
    1.  **快速接收:** `LogHandler` 收到日志，仅做最简单的校验，直接推入一个 **内存队列 (LogBuffer)**，立即返回 HTTP 200 给客户端。
    2.  **后台处理:** `Worker Pool` 中的线程不断从 `LogBuffer` 中拉取日志（可以一次拉一批，Batch Processing）。
    3.  **结果查询:** 前端查询的是数据库中“已完成”的数据。

---

## 4. 实施路线图

1.  **第一步：定义双线程池类**
    *   修改 `Server` 类，使其持有两个 `ThreadPool` 对象：`interactivePool_` 和 `workerPool_`。
    *   在 `main.cpp` 中分别初始化它们。

2.  **第二步：路由层改造**
    *   修改 `Router` 或 `Handler`，允许指定该请求由哪个线程池处理。
    *   `DashboardHandler` -> 绑定 `interactivePool_`。
    *   `LogHandler` -> 绑定 `workerPool_`。

3.  **第三步：调整参数**
    *   在 4 核机器上：IO 线程 (1) + 交互池 (1) + 工作池 (4~6)。

通过这种方式，即使后台有 10000 条日志正在排队等待 AI 分析，前端的仪表盘依然可以秒开，因为它们走的是完全不同的 VIP 通道。
