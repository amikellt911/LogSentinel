# NanoLog: A Nanosecond Scale Logging System (Original Paper)

**文件**: `atc18-yang.pdf` (USENIX ATC '18)
**作者**: Stephen Yang, Seo Jin Park, John Ousterhout (Stanford University)

## 1. 内容概要
这是 NanoLog 的**原始学术论文**。相比于之前的博客解读，论文提供了更严谨的性能数据和实现细节。

**核心贡献 (Contributions)**：
1.  **移位 (Shifting Work)**：将日志系统的开销从“运行时”转移到“编译时”和“后处理时”。
2.  **静态分析 (Static Analysis)**：在编译期自动提取日志模板，生成紧凑的 `fmtId`。
3.  **轻量级运行时 (Lightweight Runtime)**：
    *   **Uncompressed Logging**: 运行时只记录未压缩的二进制参数，避免 CPU 密集型的压缩操作。
    *   **NanoLog Protocol**: 设计了一种极简的二进制协议。
4.  **性能数据**:
    *   吞吐量：单核 **80 million logs/second**。
    *   延迟：**7 nanoseconds** per log invocation。
    *   带宽：日志文件大小是传统文本日志的 **1/10** 甚至更小。

## 2. 对 LogSentinel 项目的帮助

### 2.1 深度技术参考
*   **协议设计**: 论文详细描述了二进制日志的布局（Layout）。LogSentinel 如果要实现自定义的二进制传输协议，应直接参考论文的 **Section 3 (Design)**。
*   **原子操作**: 论文中对 `Reserve` 和 `Commit` 阶段的原子操作有详细伪代码，这是实现无锁队列的“圣经”。

### 2.2 论文写作素材
*   **权威引用**: 在论文的 Reference 列表，必须引用此文件：
    > Stephen Yang, Seo Jin Park, and John Ousterhout. "NanoLog: A Nanosecond Scale Logging System." USENIX Annual Technical Conference (ATC). 2018.
*   **性能基准 (Benchmark)**: 可以引用 NanoLog 的性能数据作为“极致性能”的上限（Upper Bound），来评估 LogSentinel 的性能水平。

## 3. 个人阅读建议
**建议阅读指数：⭐⭐⭐⭐⭐ (5/5)**
*   **理由**：作为原始论文，它是所有解读文章的源头。如果你想深入了解无锁队列的每一个比特是如何翻转的，必须读原文。
*   **重点章节**:
    *   **3.2 Runtime Logging**: 详细介绍了如何利用 `rdtsc` 获取时间戳和无锁写入。
    *   **3.3 Post-processor**: 介绍了如何还原日志。

## 4. 实现难度与优先级

### 4.1 涉及功能
*   **无锁队列实现**: 参考论文中的伪代码优化 `ThreadPool`。

### 4.2 优先级 (MVP 规划)
*   **MVP 3 (性能优化)**:
    *   **阅读原文**: 在实现 RingBuffer 前，建议开发者（我）仔细阅读论文的 Section 3.2，确保原子操作的顺序（Memory Ordering）是正确的。

## 5. 评价
原始论文提供了博客无法覆盖的细节，特别是关于**一致性**和**原子性**的讨论。
**你想要我完成的工作**：确认已归档该 PDF，并将其作为 LogSentinel 性能优化的核心理论依据。
