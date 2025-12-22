# 数据库表结构变更与影响分析报告

## 1. 变更背景
为了支持更丰富的批次分析功能（如全局风险等级、关键词标签、以及更高效的统计查询），需要对数据库表结构进行重大调整。此次调整将引入 `batch_summaries` 主表，并修改 `analysis_results` 表以建立关联。

## 2. 数据库变更详情

### 2.1 新增主表: `batch_summaries`
此表用于存储每个分析批次的宏观聚合信息。

```sql
CREATE TABLE IF NOT EXISTS batch_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    -- 1. 宏观分析结果 (AI 生成)
    global_summary TEXT,                  -- 完整的总结小作文
    global_risk_level TEXT,               -- UI显示的大字: 'CRITICAL', 'WARNING', 'SAFE'
    key_patterns TEXT,                    -- 标签数组(JSON字符串): '["#SQLInjection", "#RateLimit"]'

    -- 2. 统计快照 (由 C++ 在插入时计算，列表页直接读，避免全表 COUNT)
    total_logs INTEGER DEFAULT 0,         -- 这批一共有多少条
    cnt_critical INTEGER DEFAULT 0,       -- Critical 数量
    cnt_error    INTEGER DEFAULT 0,       -- Error 数量
    cnt_warning  INTEGER DEFAULT 0,       -- Warning 数量
    cnt_info     INTEGER DEFAULT 0,       -- Info 数量
    cnt_safe     INTEGER DEFAULT 0,       -- Safe 数量
    cnt_unknown  INTEGER DEFAULT 0,       -- Unknown 数量

    -- 3. 性能指标
    processing_time_ms INTEGER DEFAULT 0, -- 整批分析耗时 (UI上的 Analysis Duration)

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 2.2 修改从表: `analysis_results`
此表存储单条日志的微观分析结果。

*   **变更**: 新增 `batch_id` 字段作为外键。
*   **注意**: 这是一个破坏性变更，由于 SQLite `ALTER TABLE` 功能有限，且为了简化开发，采取 **直接删除旧库重建** 的策略。

```sql
CREATE TABLE IF NOT EXISTS analysis_results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    trace_id TEXT NOT NULL UNIQUE,

    -- 关联主表
    batch_id INTEGER,

    -- 微观分析结果
    risk_level TEXT,
    summary TEXT,
    root_cause TEXT,
    solution TEXT,

    -- 单条耗时
    response_time_ms INTEGER DEFAULT 0,

    -- 状态
    status TEXT NOT NULL,

    -- 时间
    processed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    FOREIGN KEY (trace_id) REFERENCES raw_logs(trace_id),
    FOREIGN KEY (batch_id) REFERENCES batch_summaries(id)
);
```

## 3. 依赖影响分析 (牵一发动全身)

### 3.1 Python AI Proxy (`server/ai/proxy`)
**影响等级**: 高
**改动内容**:
1.  **Schemas 定义**: 需要在 `schemas.py` 中定义新的 `BatchSummaryResponse` 结构，包含 `global_summary`, `global_risk_level`, `key_patterns`。
2.  **Prompt 逻辑**: 默认的 Reduce Prompt 需要更新，指导 AI 输出 JSON 格式而非纯文本。
3.  **Endpoint 逻辑**: `providers/gemini.py` 和 `main.py` 中的 `summarize` 接口需要改为返回结构化 JSON 对象。

### 3.2 C++ Core Logic (`server/core/LogBatcher.cpp`)
**影响等级**: 极高
**改动内容**:
1.  **处理流程重构**: `processBatch` 函数目前只处理字符串类型的 Summary，现在需要解析 AI 返回的 JSON 结构。
2.  **统计计算**: 在插入数据库前，需要遍历当次批次的所有 Map 结果，计算 `cnt_critical`, `cnt_error` 等 6 个计数器。
3.  **双重插入**:
    *   第一步：插入 `batch_summaries` 表，获取返回的 `batch_id` (需使用 `sqlite3_last_insert_rowid`)。
    *   第二步：将 `batch_id` 注入到每条 `AnalysisResultItem` 中，批量插入 `analysis_results`。
4.  **性能计时**: 需要准确记录 `processing_time_ms` 并存入主表。

### 3.3 C++ Persistence (`server/persistence/SqliteLogRepository.cpp`)
**影响等级**: 高
**改动内容**:
1.  **初始化**: `SqliteLogRepository` 构造函数中需更新 `CREATE TABLE` 语句。
2.  **写入接口**: `saveAnalysisResultBatch` 接口签名可能需要调整，或者内部逻辑大幅修改以支持主从表事务写入。
3.  **读取接口**: `getDashboardStats` 可能需要适配（尽管目前主要从 `analysis_results` 读，但未来可能部分指标切到 `batch_summaries`）。
4.  **历史记录**: 现有的 `getHistoricalLogs` 是基于单条日志的，可能需要新增 `getBatchHistory` 接口供“批次详情页”使用（如有）。

### 3.4 前端 (`client/`)
**影响等级**: 中
**改动内容**:
1.  **数据展示**: Dashboard 或 Insights 页面需要展示 AI 返回的 `global_risk_level` 和 `key_patterns`。
2.  **API 适配**: 如果后端 API 响应结构变化，前端 Store (`system.ts`) 需要同步更新类型定义。

## 4. 执行计划 (原子化步骤)

为了确保变更可控，建议按照以下顺序执行：

1.  **Step 1: Python Proxy & Schemas 更新**
    *   修改 `schemas.py` 增加 `BatchSummaryResponse`。
    *   更新 `gemini.py` 的 `summarize` 方法以支持 JSON Schema 输出。
    *   验证 Python 服务能返回正确的 JSON。

2.  **Step 2: C++ 数据库层重构**
    *   修改 `SqliteLogRepository.cpp` 的建表语句。
    *   实现新的 `saveBatchSummary` 和 `saveAnalysisResultBatch` (带 `batch_id`) 的底层逻辑。
    *   **Action**: 删除旧数据库文件。

3.  **Step 3: C++ 核心逻辑适配**
    *   修改 `LogBatcher.cpp` 中的 `processBatch`。
    *   增加统计计数逻辑。
    *   解析 AI JSON 响应。
    *   串联写入流程。

4.  **Step 4: 前端展示适配**
    *   确保后端 API (通过 `SqliteLogRepository` 或 Controller) 能正确透出新字段。
    *   更新前端 UI 显示新的 Global Summary 和 Tags。
