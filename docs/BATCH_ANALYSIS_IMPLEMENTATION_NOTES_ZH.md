# 批次分析与性能优化实现笔记 (Batch Analysis Implementation Notes)

本文档记录了为了支持“批次宏观分析”及“Dashboard 性能优化”所做的核心修改、踩坑记录以及关键技术点说明。

## 1. 修改内容概览

### 1.1 Python AI Proxy (`server/ai/proxy/`)
*   **目标**: 让 AI 返回结构化的宏观分析结果，而不是纯文本。
*   **修改**:
    *   `schemas.py`: 新增 `BatchAnalysisOutput` 模型，包含 `global_summary`, `global_risk_level`, `key_patterns`。
    *   `providers/gemini.py`: 修改 `summarize` 方法，配置 `response_schema` 为 `BatchAnalysisOutput`，强制 AI 输出 JSON。

### 1.2 C++ 存储层 (`server/persistence/`)
*   **目标**: 支持存储批次总结，并优化 Dashboard 的高频读取性能。
*   **修改**:
    *   `ConfigTypes.h`: 更新 `DashboardStats` 结构体，新增 `current_qps` 和 `backpressure` (瞬态指标)，并注册到 `NLOHMANN_DEFINE_TYPE_INTRUSIVE`。
    *   `SqliteLogRepository`:
        *   **Schema**: 新增 `batch_summaries` 表；`analysis_results` 表增加 `batch_id` 外键。
        *   **内存快照 (Write-Through Cache)**: 引入 `cached_stats_` 指针。启动时加载一次 DB，之后每次写入 `saveAnalysisResultBatch` 时原子更新内存快照。
        *   **接口**: 新增 `saveBatchSummary` 和 `updateRealtimeMetrics`。

### 1.3 C++ 核心逻辑 (`server/core/`)
*   **目标**: 串联 AI 结果解析与指标计算。
*   **修改**:
    *   `LogBatcher`:
        *   **JSON 解析**: 在 `processBatch` 中解析 AI 返回的 JSON 字符串。
        *   **双写逻辑**: 先写 `saveBatchSummary` 获取 ID，再写 `analysis_results`。
        *   **QPS 计算**: 利用 `onTimeout` 定时器（每 200ms），每 1000ms (1s) 计算一次 QPS 和队列水位，并通过 `repo_->updateRealtimeMetrics` 推送给存储层。这是为了配合前端图表的 1秒 精度。

## 2. 踩坑与修复记录 (Pitfalls & Fixes)

### 2.1 单元测试的文件权限问题
*   **问题**: 运行 `test_logbatcher` 时报错 `filesystem error: cannot create directories: Permission denied [../persistence/data]`。
*   **原因**: 测试代码试图在构建目录之外创建文件夹，或者路径相对位置不对。
*   **解决**: 在 `LogBatcher_test.cpp` 中将测试数据库路径改为 SQLite 的内存模式 `":memory:"`。这不仅解决了权限问题，还让测试更快、更干净（无需清理文件）。

### 2.2 结构体定义缺失导致编译失败
*   **问题**: 编译 `SqliteLogRepository` 时报错 `DashboardStats has no member named current_qps`。
*   **原因**: 在 `SqliteLogRepository.cpp` 里使用了新字段，但忘记同步更新 `ConfigTypes.h` 中的结构体定义。
*   **解决**: 立即修改 `server/persistence/ConfigTypes.h`，添加 `current_qps` 和 `backpressure` 字段。

### 2.3 `nlohmann::json` 的枚举序列化
*   **注意**: C++ 的 `enum` 即使定义了 `NLOHMANN_JSON_SERIALIZE_ENUM`，在手动解析 JSON 对象时（`j.value(...)`），如果不小心，可能会得到字符串而不是 Enum 类型，或者需要显式转换。
*   **实践**: 在 `LogBatcher.cpp` 解析 AI 返回的 JSON 时，`global_risk_level` 是字符串，需要留意它与 C++ Enum 的对应关系（目前作为字符串存储和传递）。

## 3. 核心技术点 (For Newbies)

如果你是刚接手这部分代码的新手，请注意以下设计模式：

### 3.1 写穿透缓存 (Write-Through Caching)
*   **场景**: Dashboard 每秒轮询 `/api/dashboard/stats`，如果每次都去 SQLite `SELECT SUM(...)`，QPS 一高数据库就崩了。
*   **解法**:
    *   **读**: Dashboard 直接读内存里的 `std::shared_ptr<DashboardStats>`，耗时 0ms，不走 IO。
    *   **写**: 每次 `LogBatcher` 写入一批日志到 DB 成功后，**顺便**把这批日志的统计数累加到内存快照里。
    *   **一致性**: 虽然内存是“缓存”，但因为它是写操作的最后一步（事务提交后），所以它和数据库是**强一致**的（Eventually Consistent within microseconds）。

### 3.2 瞬态指标 (Transient Metrics)
*   **概念**: `current_qps` (吞吐率) 和 `backpressure` (积压率) 是反映系统“当前状态”的数字。
*   **存储**: 它们不需要存入 SQLite 数据库（存了也没意义，历史 QPS 一般用时序数据库存，或者只看日志时间戳算）。
*   **实现**: `LogBatcher` 负责算，算完推给 `SqliteLogRepository` 的内存快照。Dashboard 查的时候，顺带就查出来了。重启服务器后，这些数字归零，这是符合预期的。

### 3.3 定时器策略
*   **代码**: `loop_->runEvery(0.2, ...)`
*   **注意**: 不要为了“精确”而在回调里反复 `reset` 定时器。固定频率的轮询（Tick）配合状态检查（`elapsed_ms >= 1000`）是开销最小、最稳定的做法。

---
**文档作者**: Jules
**日期**: 2024-05-23
