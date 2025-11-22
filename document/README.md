# LogSentinel 外部资源文档库

本文档库记录了所有对 LogSentinel 项目开发和论文写作有帮助的外部资源。

| ID | 标题 | 建议星级 | 关键内容摘要 | 对项目的关键作用和帮助 |
|----|------|---------|-------------|----------------------|
| 01 | [美团 Logan 高性能日志系统 (美团技术)](part/01_meituan_logan.md) | ⭐⭐⭐⭐ | WAL 机制、背压控制、加密存储 | 验证 WAL 设计合理性；为 MVP2 背压机制提供参考 |
| 02 | [免费开源 AI 日志工具 (CSDN)](part/02_csdn_ai_log_tools.md) | ⭐⭐⭐ | Drain3 日志解析、DeepLog 异常检测 | Drain3 可作为前置过滤器，减少 LLM 调用成本 |
| 03 | [什么是 AI 日志分析 (IBM)](part/03_ibm_ai_log_analysis.md) | ⭐⭐ | AI 日志分析概念科普、合成日志生成 | 验证 Chat 接口的前瞻性；合成日志提供测试思路 |
| 04 | [事件驱动型架构 (Google Cloud)](part/04_google_event_driven.md) | ⭐⭐⭐⭐ | EDA 核心概念、幂等性、解耦 | 为 LogSentinel 架构提供理论背书；强调幂等性 |
| 05 | [高性能日志系统设计与优化 (知乎)](part/05_zhihu_high_perf_log.md) | ⭐⭐⭐⭐⭐ | RingChunkBuff、Chunk、自旋锁、C++ 实现 | LogSentinel 持久化层和线程池设计的直接参考 |
| 06 | [cpplog: C++ 高性能日志系统 (GitHub)](part/06_github_cpplog.md) | ⭐⭐⭐⭐ | RingChunkBuff 源码、配置调优、位运算优化 | 直接的代码参考；环形缓冲区实现逻辑 |
| 07 | [NanoLog: 纳秒级高性能日志系统 (ATC '18)](part/07_nanolog_paper.md) | ⭐⭐⭐⭐⭐ | 编译期静态提取、运行时只记参数、离线解码 | 启发"结构化传输"以节省带宽和 Token；顶会论文引用 |
| 08 | [NanoLog 原始论文 (PDF)](part/08_nanolog_original_paper.md) | ⭐⭐⭐⭐⭐ | 原始论文文件、详细的原子操作伪代码、性能基准 | 性能优化的"圣经"；提供无锁队列的精确实现细节 |
| 09 | [SelectDB (Apache Doris) 日志方案](part/09_selectdb_log_analysis.md) | ⭐⭐⭐ | 替代 ELK、列式存储、SQL 分析、倒排索引 | 未来存储选型参考 (去 ES 化)；论证 SQL 分析日志的可行性 |
| 10 | [go-logger: 高性能 Golang 日志库 (GitHub)](part/10_github_go_logger.md) | ⭐⭐⭐ | 高并发性能 (10x)、丰富的日志轮转策略 | Go SDK 设计参考；日志轮转功能对标 |
| 11 | [高性能日志模块设计 (CSDN)](part/11_csdn_high_perf_log_design.md) | ⭐⭐⭐⭐⭐ | 分层架构、LMAX Disruptor、分级存储、零拷贝 | 工业级架构蓝图；启发"分级压缩"和"归档"功能 |
| 12 | [ByteHouse 云原生日志分析 (AWS)](part/12_bytehouse_cloud_native.md) | ⭐⭐⭐ | ClickHouse + 倒排索引、FST、存算分离 (S3) | 验证 OLAP+倒排索引 路线；启发 S3 归档功能 |
| 13 | [Loghub: AI 日志分析数据集 (GitHub)](part/13_loghub_datasets.md) | ⭐⭐⭐⭐⭐ | HDFS, Spark, Linux, Apache 等真实日志数据集 | AI 模型训练/验证的金矿；论文实验的标准基准 |
| 14 | [LogLens: 实时日志分析系统 (ICDCS '18)](part/14_ieee_loglens.md) | ⭐⭐⭐⭐ | 无监督学习、实时解析、有状态分析 | 启发"前置异常检测"以降低 LLM 成本；强调上下文关联 |
| 15 | [Top 9 Log Analysis Tools (SpectralOps)](part/15_spectralops_top_tools.md) | ⭐⭐⭐ | 竞品分析 (Splunk, Datadog, SpectralOps) | 启发"敏感数据检测/脱敏"功能；明确"轻量级+AI原生"的市场定位 |
| 16 | [LogAI: 开源日志智能分析库 (GitHub)](part/16_github_logai.md) | ⭐⭐⭐⭐ | 统一分析流水线、OpenTelemetry 兼容、集成多种 ML 模型 | 模块化设计参考；OTel 数据模型对标；论文对比基线 |
| 17 | [AI 日志分析指南 (LogicMonitor)](part/17_logicmonitor_ai_logs.md) | ⭐⭐⭐ | 降噪、基线学习、CrowdStrike 案例 | 启发"基线重置"和"用户反馈"机制；提供论文背景动机 |
| 18 | [AI 日志分析器实践 (Medium)](part/18_medium_ai_log_analyzer.md) | ⭐⭐ | 概念验证、无源码 | 验证 AI+日志 的市场需求；无直接技术参考 |
| 19 | [AI 日志监控指南 (Splunk)](part/19_splunk_ai_monitoring.md) | ⭐⭐⭐ | 多云挑战、动态阈值、隐私保护 (PII Masking) | 强化"敏感数据脱敏"的必要性；提供论文安全设计参考 |
| 20 | [LLM 日志分析技术综述 (arXiv 2025)](part/20_arxiv_llm_log_survey.md) | ⭐⭐⭐⭐⭐ | 异常检测、RCA、RAG、Fine-tuning、Concept Drift、False Positives | 论文写作的权威引用；指明技术挑战和解决方向 |
| 21 | [AI 异常检测与根因分析 (SSRN 2025)](part/21_ssrn_ai_anomaly_rca.md) | ⭐⭐⭐⭐⭐ | Logs/Metrics/Traces 联合分析、GNN、Autoencoder、案例研究 (Adobe, Uber) | LogSentinel 的"精神伴侣"；论文核心引用；多模态分析参考 |
| 22 | [基于 LSTM+VAE 的无监督检测 (中文期刊)](part/22_chinese_lstm_vae.md) | ⭐⭐⭐⭐ | LSTM+VAE 混合架构、LOF 自适应阈值、重构误差 | 提供"动态阈值"的具体算法 (LOF)；丰富 Related Work |
| 23 | [基于 TCN 的轻量级异常检测 (中文期刊)](part/23_chinese_tcn_lightweight.md) | ⭐⭐⭐⭐ | TCN、深度可分离卷积、全局平均池化 (GAP)、边缘计算 | 为 LogSentinel Agent 端的"边缘智能"提供架构参考 (轻量化) |
| 24 | [面向系统日志的异常检测 (中文硕论)](part/24_chinese_thesis_log_anomaly.md) | ⭐⭐⭐ | 参数感知 (Parameter-Aware)、域自适应、ALBERT-D | 强调"日志参数"的重要性；提供 2023/2024 最新故障案例 |
