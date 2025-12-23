# 数据库与架构变更分析文档 (Batch Analysis & Optimization)

## 1. 核心变更目标
为了支持“批次宏观分析”功能并优化高频 Dashboard 查询性能，我们需要对数据库结构进行升级，引入内存快照机制，并同步调整后端 AI 处理流程。

目标是让系统能够：
1.  **宏观总结 (Macro Summary)**: 存储 AI 生成的整批日志的总结、全局风险等级、关键词标签。
2.  **统计快照 (Write-Through Caching)**:
    *   在写入 DB 时同步更新内存中的统计快照。
    *   Dashboard 轮询请求直接读取内存快照，实现 0 IO 延迟。
3.  **结构化输出 (Structured Output)**: 强制 AI 返回 JSON 格式，包含总结和风险计数。
4.  **实时指标 (Real-time Metrics)**:
    *   由 `LogBatcher` 计算 QPS 和背压（Backpressure）。
    *   定期推送到 Repository 的内存快照中。

## 2. 数据库 Schema 变更详情

### 2.1 主表: `batch_summaries` (新增)
用于存储批次维度的聚合信息。

```sql
CREATE TABLE IF NOT EXISTS batch_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    -- 1. AI 宏观分析结果 (来源: Python AI Proxy JSON)
    global_summary TEXT,                  -- 批次总结内容
    global_risk_level TEXT,               -- 全局风险等级: 'CRITICAL', 'WARNING', 'SAFE' 等
    key_patterns TEXT,                    -- 关键标签数组(JSON): '["#SQLInjection", "#RateLimit"]'

    -- 2. 统计快照 (来源: C++ Batcher 计算)
    total_logs INTEGER DEFAULT 0,         -- 总条数

    -- 风险分布计数 (6个维度)
    cnt_critical INTEGER DEFAULT 0,
    cnt_error    INTEGER DEFAULT 0,
    cnt_warning  INTEGER DEFAULT 0,
    cnt_info     INTEGER DEFAULT 0,
    cnt_safe     INTEGER DEFAULT 0,
    cnt_unknown  INTEGER DEFAULT 0,

    -- 3. 性能指标
    processing_time_ms INTEGER DEFAULT 0, -- 批次处理总耗时

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 2.2 从表: `analysis_results` (重构)
存储单条日志详情，关联主表。
**迁移策略**: 采用 **直接重建**。服务端启动时若发现表结构不匹配，删除旧 DB 文件重建。

```sql
CREATE TABLE IF NOT EXISTS analysis_results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    trace_id TEXT NOT NULL UNIQUE,

    -- 外键关联
    batch_id INTEGER,

    -- 微观分析结果
    risk_level TEXT,
    summary TEXT,
    root_cause TEXT,
    solution TEXT,

    -- 状态与耗时
    status TEXT NOT NULL,
    response_time_ms INTEGER DEFAULT 0,

    FOREIGN KEY (trace_id) REFERENCES raw_logs(trace_id),
    FOREIGN KEY (batch_id) REFERENCES batch_summaries(id)
);
```

## 3. 内存快照策略 (DashboardStats)

为了解决 Dashboard 高频轮询导致的数据库压力，采用 "Write-Through Caching" 策略。

### 3.1 数据结构 (`DashboardStats`)
```cpp
struct DashboardStats {
    // --- 累加型指标 (Persistent) ---
    long long total_logs = 0;
    long long cnt_critical = 0;
    long long cnt_error = 0;
    long long cnt_warning = 0;
    long long cnt_info = 0;
    long long cnt_safe = 0;
    long long cnt_unknown = 0;

    // --- 瞬态型指标 (Transient) ---
    double current_qps = 0.0;          // 实时吞吐
    double backpressure_ratio = 0.0;   // 队列水位 (0.0 - 1.0)
};
```

### 3.2 读写流程
1.  **启动 (Cold Start)**: `SqliteLogRepository` 构造时执行一次 SQL `SUM(...)`，初始化内存中的 `cached_stats_`。
2.  **写入 (Write Path)**: `saveAnalysisResultBatch` 在事务提交后，原子更新内存快照（累加计数）。
3.  **读取 (Read Path)**: `getDashboardStats()` 直接返回内存快照指针 (shared_ptr)，无锁争用，零 IO。
4.  **实时更新 (Metrics Push)**: `LogBatcher` 定时（如每 500ms）计算 QPS 和水位，调用 `updateRealtimeMetrics` 更新快照中的瞬态字段。

## 4. 原子化任务执行顺序

为了避免错误级联，严格按照以下顺序执行：

1.  **任务一：Python AI 接口标准化**
    *   **目标**: 确保 AI 输出可解析的 JSON。
    *   **修改**:
        *   `server/ai/proxy/schemas.py`: 新增 `BatchAnalysisOutput` Pydantic 模型。
        *   `server/ai/proxy/providers/gemini.py`: 更新 `summarize` 方法，使用 `response_schema`。

2.  **任务二：C++ 数据库层重构与缓存实现**
    *   **目标**: 实现 Schema 变更和内存快照逻辑。
    *   **修改**:
        *   `SqliteLogRepository.h`: 定义 `DashboardStats` 结构，增加 `cached_stats_` 成员。
        *   `SqliteLogRepository.cpp`: 实现 `rebuildStatsFromDb` (启动加载), `saveBatchSummary` (写入主表), `updateRealtimeMetrics` (瞬态更新)。
        *   **注意**: 处理好旧数据库文件的重建逻辑。

3.  **任务三：C++ 核心业务逻辑实现**
    *   **目标**: 串联统计、AI 调用、指标计算。
    *   **修改**:
        *   `LogBatcher.h/cpp`:
            *   在 `processBatch` 中统计 Risk 计数。
            *   解析 AI 返回的 JSON。
            *   在 `onTimeout` 中计算 QPS 和 Backpressure 并推送到 Repository。

4.  **任务四：联调与验收**
    *   **验证**:
        *   启动服务，检查 DB Schema。
        *   发送日志，检查 Dashboard 实时数据（QPS, 风险计数）是否跳动。
        *   检查 `batch_summaries` 表是否有数据。
