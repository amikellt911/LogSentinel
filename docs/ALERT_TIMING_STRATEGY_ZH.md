# 告警时间戳与处理策略分析 (Alert Timing Strategy)

本文档针对 PR 评论中关于 "AnalysisResultItem 是否需要增加 process_time" 以及 "Recent Alerts 如何处理" 的问题进行分析和建议。

## 1. 问题背景
在 `SqliteLogRepository::saveAnalysisResultBatch` 中构建 `DashboardStats` 的内存快照时，我们需要将高风险日志（Critical）加入到 `recent_alerts` 列表中。这就涉及到一个问题：**这个 Alert 的时间戳 (`time`) 应该取什么时间？**

目前代码的做法是：在 `saveAnalysisResultBatch` 函数执行时，获取当前的系统时间 (`time(0)` + `localtime`) 作为告警时间。

用户提议：是否应该在 `AnalysisResultItem` 结构体中透传一个 `process_time`？

## 2. 方案对比

### 方案 A：在 `AnalysisResultItem` 中增加 `process_time`
*   **实现**: 在 `LogBatcher` 处理每个 Task 时，记录该 Task 完成 AI 分析的具体时间点，并存入 `AnalysisResultItem`。
*   **优点**: 时间戳最精确，反映了 AI "产生" 结果的那一瞬间。
*   **缺点**: 需要修改 `AnalysisResultItem` 结构体，`LogBatcher` 需要多一步赋值。
*   **必要性分析**: `LogBatcher` 处理一批日志通常在几百毫秒内完成。对于 "Dashboard 告警" 这种秒级粒度的展示场景，"AI 分析完成时间" 与 "写入数据库时间" (方案 B) 之间的差异通常在毫秒级，肉眼不可见。

### 方案 B：在持久化时使用当前系统时间 (Current Implementation)
*   **实现**: `saveAnalysisResultBatch` 执行时，调用 `time(0)` 获取当前时间。
*   **定义**: 这个时间代表 **"告警被系统确认并持久化的时间"** (Detection & Persistence Time)。
*   **优点**:
    *   代码最简洁，无需侵入 `AnalysisResultItem`。
    *   逻辑自洽：只有当结果被写入 DB 且更新缓存时，它才正式成为一个 "Alert"。
*   **适用性**: 对于 Dashboard 的 "Recent Alerts" 列表，用户关注的是 "最近什么时候报了警"，方案 B 完全满足需求。

### 方案 C：使用日志原始时间 (`received_at` / `start_time`)
*   **实现**: 透传 `AnalysisTask` 的开始时间。
*   **定义**: 代表 **"日志产生或到达的时间"**。
*   **问题**: 如果系统积压严重，日志到达时间和告警时间可能相差很久。对于运维监控，通常更关注 "什么时候检测到了风险" (Alert Time)，而不是 "风险日志上面写的是几点" (Log Time)。

## 3. 为什么在 `SqliteLogRepository` 处理 Recent Alerts？

这是一个经典的 **Write-Through Caching (写穿透缓存)** 模式优化。

1.  **性能**: 如果每次 Dashboard 刷新都去查 `SELECT * FROM analysis_results WHERE level='critical' ORDER BY id DESC LIMIT 5`，虽然有索引，但仍然是一次 DB IO。
2.  **效率**: 在 `saveAnalysisResultBatch` 中，我们**正好**拿着这一批新出炉的高风险结果。此时顺手把它们塞进内存里的 `recent_alerts` 列表（std::vector），代价几乎为零（纯内存操作）。
3.  **一致性**: 这种方式保证了 Dashboard 看到的告警列表与数据库写入是原子同步的。

## 4. 结论与建议

**建议维持当前实现 (方案 B)**，理由如下：

1.  **精确度足够**: `AnalysisResultItem` 从生成到保存的时间差极短，为此增加字段属于过度设计。
2.  **语义正确**: Dashboard 上的 "Alert Time" 通常指 "Detection Time"（系统发现问题的时间），而在持久化层打时间戳最符合这个定义。
3.  **架构清晰**: `LogBatcher` 负责计算，`Repository` 负责存储和维护视图状态。

如果未来确实需要区分 "AI 分析耗时" 和 "排队耗时"，或者需要毫秒级精确的 Trace 分析，再考虑升级到方案 A。目前阶段，方案 B 是性价比最高的选择。
