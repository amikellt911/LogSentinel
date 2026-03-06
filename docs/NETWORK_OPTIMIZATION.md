# 网络深度优化指南 (Network Optimization)

本文档旨在结合计算机网络理论（“八股”）与本项目实际架构，探讨 **IO 模型、TCP 协议特性、以及应用层优化**，既作为技术复习，也是后续性能调优的实战指南。

## 1. IO 多路复用 (IO Multiplexing)

本项目基于 **MiniMuduo** 网络库，其核心就是 Reactor 模式与 `epoll`。

### 1.1 理论基础 (The Theory)
*   **阻塞 IO (Blocking):** 一个线程只能处理一个连接。`read()` 会一直等到有数据才返回。并发量受限于线程数。
*   **非阻塞 IO (Non-blocking):** `read()` 没数据立即返回 EWOULDBLOCK。需要轮询，CPU 空转。
*   **IO 多路复用 (Multiplexing):** 操作系统提供 `select` / `poll` / `epoll`。一个线程可以监控成千上万个 socket。当某个 socket 可读/可写时，内核通知应用程序。
    *   **LT (Level Triggered):** 只要有数据没读完，内核就一直通知你。（MiniMuduo 默认模式，编程简单，防漏读）。
    *   **ET (Edge Triggered):** 状态变化时只通知一次。必须一次性把数据读完。（Nginx 模式，高性能但编程极难）。

### 1.2 实战应用 (Project Application)
*   **为什么快？** 我们的 LogSentinel 只需要 1 个 IO 线程就可以维持数万个并发连接（日志 Agent）。
*   **架构警示:** Reactor 模式最忌讳 **在 IO 线程里执行耗时任务**（如 AI 分析、复杂 SQL）。这会阻塞 EventLoop，导致所有其他连接的 `epoll_wait` 无法及时返回，发生“排队雪崩”。
*   **解决方案:** 这正是我们在 `docs/FUTURE_ARCHITECTURE.md` 中设计 **“交互线程池 + 工作线程池”** 的根本原因——将计算剥离，还 IO 线程以自由。

---

## 2. TCP_NODELAY (禁用 Nagle 算法)

### 2.1 理论基础
*   **Nagle 算法:** 为了减少网络拥塞，避免发送大量的小包（Tinygram）。它规定：如果有未确认的数据（Unacked Data），小包必须先缓存起来，凑够一个 MSS（最大报文段）或者收到 ACK 再发。
*   **延迟确认 (Delayed ACK):** 接收方收到数据后，不马上回 ACK，而是等 40ms 看看有没有数据要回传（捎带确认）。
*   **可怕的 40ms:** 当 Nagle 遇到 Delayed ACK，会导致经典的 **40ms 延迟** 问题。

### 2.2 实战优化
*   **场景:** 我们的 `/dashboard` 接口返回的 JSON 虽然不大，但要求 **实时性**。如果 Nagle 算法开启，前端看到的曲线可能会有微妙的卡顿。
*   **建议:** 对于 API 服务，通常建议 **全局开启 `TCP_NODELAY`**。
*   **代码位置:** 在 `HttpServer::onConnection` 中调用 `conn->setTcpNoDelay(true)`。

---

## 3. HTTP Keep-Alive (长连接)

### 3.1 理论基础
*   **短连接:** 每次请求都经历 `SYN -> SYN+ACK -> ACK` (3次握手) 和 `FIN...` (4次挥手)。
*   **开销:** 握手增加 1 个 RTT 延迟；频繁创建销毁 Socket 消耗 CPU；服务器产生大量 `TIME_WAIT` 状态的 Socket，占用端口资源。
*   **Keep-Alive:** 复用同一条 TCP 连接发送多个 HTTP 请求。

### 3.2 实战应用
*   **现状:** 前端 `setInterval` 每秒轮询一次。
*   **优化:** 必须确保前端（浏览器默认开启）和后端都支持 Keep-Alive。
*   **后端检查:** 检查 `HttpServer.cpp`，确保在 `HTTP/1.1` 下，除非收到 `Connection: close`，否则不要主动断开连接。
*   **收益:** 此时前端的 1000 次轮询，只会产生 1 次 TCP 握手。QPS 承载能力将大幅提升。

---

## 4. 零拷贝 (Zero-Copy)

### 4.1 理论基础
*   **传统读写:** 硬盘 -> 内核缓冲区 -> 用户缓冲区 -> 内核 Socket 缓冲区 -> 网卡。数据被拷贝 4 次，上下文切换 4 次。
*   **Sendfile:** 硬盘 -> 内核缓冲区 -> 网卡。数据只在内核中传递，**零**用户态拷贝。

### 4.2 实战应用
*   **场景:** 如果我们要在 C++ 后端直接托管前端静态资源（`dist/index.html`, `.js`, `.css`），或者提供日志文件下载功能。
*   **建议:** 对于大于 1KB 的静态文件响应，不应该用 `read()` 读到 `std::string` 再 `send()`。应该使用 `Linux sendfile` 系统调用（MiniMuduo 暂未封装，可作为高级功能自行扩展）。

---

## 5. 序列化 (Serialization)

### 5.1 理论基础 (OSI 表示层)
*   **文本协议 (JSON/XML):** 可读性好，但解析慢（字符串匹配、浮点数转换），体积大。
*   **二进制协议 (Protobuf/Thrift):** 紧凑，解析极快（位运算），不可读。

### 5.2 实战应用
*   **现状:** 使用 JSON。
*   **瓶颈:** 在之前的性能分析中，Python 的 `json.loads` 是 CPU 杀手。C++ 的 `nlohmann::json` 虽然快，但也是 CPU 密集型操作。
*   **未来:** 如果日志量达到百万级 QPS，应考虑让日志 Agent（客户端）直接发送 **Protobuf** 编码的二进制流，后端直接解包入库，CPU 占用率可下降 50% 以上。
