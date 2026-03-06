# API 需求与接口文档

本文档概述了前端客户端所需的 API 接口，并详细说明了现有的后端实现。

## 后端基础 URL
假设为 `http://localhost:8080`（或通过环境变量/代理配置）。

## 接口列表

### 1. 仪表盘统计 (Dashboard Stats)
**接口地址:** `GET /dashboard`

**描述:** 返回仪表盘所需的聚合统计数据。

**后端实现 (`DashboardStats`):**
```json
{
  "total_logs": 12345,
  "high_risk": 5,
  "medium_risk": 10,
  "low_risk": 20,
  "info_risk": 100,
  "unknown_risk": 0,
  "avg_response_time": 0.45,
  "recent_alerts": [
    {
      "trace_id": "uuid-1",
      "summary": "Detected SQL Injection",
      "time": "2023-10-27 10:00:00"
    }
  ]
}
```

**前端需求 (映射关系):**
- `totalLogsProcessed` -> `total_logs`
- `netLatency` -> `avg_response_time` (部分映射，前端期望单位为 ms，后端需确认单位)
- `riskStats`:
    - `Critical` -> `high_risk`
    - `Error` -> `medium_risk`
    - `Warning` -> `low_risk`
    - `Info` -> `info_risk`
    - `Safe` -> (计算得出: `total_logs` - 所有风险之和)
- `recentAlerts` -> `recent_alerts`

**缺失/差距:**
- **系统指标:** 前端显示服务器内存使用率 (Memory Usage)、队列深度/背压状态 (Queue/Backpressure)。目前后端未提供这些数据。
- **AI 指标:** `aiLatency` (AI 延迟), `aiTriggerCount` (AI 触发次数)。这些不在目前的 `DashboardStats` 中。
- **时间序列图表:** 前端显示 QPS (摄入率) 和 AI 处理率的实时图表。目前后端仅返回快照数据，未提供历史趋势数据。

### 2. 历史日志 (Historical Logs)
**接口地址:** `GET /history`
**查询参数:** `?page=1&pageSize=10`

**后端实现 (`HistoryPage`):**
```json
{
  "logs": [
    {
      "trace_id": "uuid-123",
      "risk_level": "Critical",
      "summary": "Detected malicious payload",
      "processed_at": "2023-10-27 10:05:00"
    }
  ],
  "total_count": 100
}
```

**前端需求 (映射关系):**
- Store 中的 `logs` 数组。
- 前端对象结构:
  ```ts
  interface LogEntry {
      id: number | string
      timestamp: string
      level: 'INFO' | 'WARN' | 'RISK'
      message: string
  }
  ```
- 映射:
    - `id` -> `trace_id`
    - `timestamp` -> `processed_at`
    - `level` -> `risk_level` (需要标准化，后端返回字符串如 "Critical"，前端期望特定的枚举值)
    - `message` -> `summary`

### 3. 日志提交 (Log Submission)
**接口地址:** `POST /logs`

**请求体:**
```json
{
    "trace_id": "optional-client-trace-id",
    "body_": "Raw log content..."
}
```

**响应:**
```json
{
    "trace_id": "uuid-generated-or-echoed"
}
```

### 4. 分析结果 (Analysis Result)
**接口地址:** `GET /results/:trace_id`

**响应:**
返回特定日志的详细分析结果。

## 后端待办事项 (未来规划)
为了完全支持前端仪表盘，后端需要提供：
1.  **系统健康指标:** 在 `GET /dashboard` 中添加内存使用率、CPU 使用率和队列状态。
2.  **时间序列数据:** 提供查询 QPS 历史数据（例如过去 60 秒）的方法，用于绘制主图表。
