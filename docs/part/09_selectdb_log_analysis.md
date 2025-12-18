# SelectDB (Apache Doris): 低成本高性能日志分析方案

**链接**: [https://www.selectdb.com/blog/654](https://www.selectdb.com/blog/654)

## 1. 内容概要
这篇文章介绍了基于 **Apache Doris (SelectDB)** 的日志存储与分析方案，旨在替代传统的 **ELK (Elasticsearch + Logstash + Kibana)** 架构。

**核心痛点 (ELK 的问题)**：
*   **写入吞吐低**：构建倒排索引消耗大量 CPU。
*   **存储成本高**：数据膨胀率高，压缩比低。
*   **查询能力弱**：DSL 学习成本高，不支持复杂 SQL（如 JOIN）。

**SelectDB 优势**：
1.  **高性能**：写入速度是 ES 的 5 倍，查询性能是 ES 的 2.3 倍。
2.  **低成本**：列式存储 + 高压缩比（1:10），存储空间仅为 ES 的 20%。
3.  **SQL 支持**：兼容 MySQL 协议，支持标准 SQL 进行复杂分析。
4.  **倒排索引**：Doris 2.0 引入了倒排索引，支持全文检索，兼顾了 ES 的搜索能力。

**架构特点**：
*   **MPP 架构**：大规模并行处理。
*   **存算分离**：支持云原生架构，弹性伸缩。

## 2. 对 LogSentinel 项目的帮助

### 2.1 项目开发参考
*   **存储选型思考**：LogSentinel 目前 MVP 阶段使用 SQLite。未来如果需要支持海量日志，**Apache Doris** 是一个比 Elasticsearch 更轻量、更高效的替代方案。特别是它支持 SQL，这与我们目前的 SQLite/MySQL 路线图非常契合，迁移成本低。
*   **列式存储**：文章强调了列式存储在日志场景下的巨大优势（高压缩比）。LogSentinel 在设计数据库表结构时，应尽量将字段拆分（如 `timestamp`, `level`, `service`），以便未来迁移到列存数据库。

### 2.2 论文写作素材
*   **竞品对比 (State of the Art)**：在论文中讨论“存储层”选型时，可以引用 SelectDB/Doris 作为“新一代日志数仓”的代表，对比传统的 ELK 方案。
*   **成本分析**：引用文章中的数据（存储节省 80%），论证“列式存储 + SQL”在日志分析领域的潜力。

## 3. 个人阅读建议
**建议阅读指数：⭐⭐⭐ (3/5)**
*   **理由**：这是一篇典型的厂商软文，数据可能存在 Cherry-picking（挑樱桃），但其提出的 **"SQL for Logs"** 和 **"Columnar Storage for Logs"** 的大方向是正确的。
*   **重点关注**：ELK 缺点的总结，以及 SelectDB 如何通过倒排索引实现全文检索。

## 4. 实现难度与优先级

### 4.1 涉及功能
1.  **Doris 集成**：未来将存储后端从 SQLite 切换到 Doris。

### 4.2 优先级 (MVP 规划)
*   **MVP 4+ (未来规划)**：
    *   **存储层升级**：当单机 SQLite 撑不住时，优先考虑引入 Doris 而不是 ES。因为 LogSentinel 的核心分析逻辑是基于 SQL 的，Doris 可以无缝衔接。

## 5. 评价
文章提供了一个很好的**“去 ES 化”**思路。对于 LogSentinel 这样追求轻量级和高性能的系统，Doris 确实比 Java 写的笨重 ES 更具吸引力。
**你想要我完成的工作**：在论文的 Future Work 中提到，计划支持 Apache Doris 作为后端存储，以应对 PB 级日志规模。
