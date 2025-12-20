# 前端接口分析与规范文档

本文档基于现有前端代码（`client/src/stores/system.ts` 和 `client/src/views/`）分析得出的接口需求。它详细描述了前端当前尝试调用的接口、数据格式以及预期的行为。

**最后更新时间:** 2024-05-23
**状态:** 现行 (Based on Code)

## 1. 全局约定

### 1.1 风险等级 (Risk Levels)
前端使用以下标准风险等级，后端返回的字符串应严格匹配（大小写敏感或后端需做归一化）：
- `Critical` (严重) - 对应旧版 `High`
- `Error` (错误) - 对应旧版 `Medium`
- `Warning` (警告) - 对应旧版 `Low`
- `Info` (信息)
- `Safe` (安全)

### 1.2 交互模式
- **轮询 (Polling):** 仪表盘和实时日志采用 HTTP 短轮询机制，间隔约为 **1000ms**。
- **模拟模式 (Simulation Mode):** 当 API 请求失败时，前端会自动切换到模拟模式，生成虚假数据以演示 UI 功能。后端开发者在调试时需注意区分是“后端未响应”还是“前端显示的是模拟数据”。

---

## 2. 仪表盘 (Dashboard)

### 2.1 获取仪表盘统计数据
前端每秒调用此接口以更新顶部的统计卡片和图表。

- **接口地址:** `GET /api/dashboard`
- **前端调用位置:** `system.ts -> fetchDashboardStats()`

**请求参数:** 无

**期望响应 (JSON):**

```json
{
  "total_logs": 1245890,          // 总处理日志数 (对应前端 totalLogsProcessed)
  "critical_risk": 2,             // 严重风险数
  "error_risk": 15,               // 错误风险数
  "warning_risk": 45,             // 警告风险数
  "info_risk": 120,               // 信息日志数
  "safe_risk": 8500,              // 安全日志数 (若缺失，前端会尝试通过减法计算)
  "unknown_risk": 0,              // 未知
  "avg_response_time": 0.05,      // 平均处理延迟 (ms) (对应前端 netLatency)
  "latest_batch_summary": "...",  // (可选) 最近一次批处理的摘要文本
  "recent_alerts": [              // 最近的报警列表
    {
      "trace_id": "uuid-...",
      "time": "10:23:01",
      "summary": "SQL Injection pattern detected..."
    }
  ]
}
```

**差距分析:**
- **缺失指标:** 前端 store 中定义了 `memoryPercent` (内存使用率), `qps` (每秒查询数), `aiRate` (AI处理率), `queuePercent` (队列深度)。目前 `DashboardStats` 响应结构中尚未明确包含这些字段，前端目前多依赖模拟生成图表数据。
- **建议:** 后端应在响应中增加 `system_metrics` 对象包含 `cpu_usage`, `memory_usage`, `current_qps` 等实时指标。

---

## 3. 历史日志 (History)

### 3.1 获取分页日志
- **接口地址:** `GET /api/history`
- **前端调用位置:** `system.ts -> fetchLogs()` (用于实时流) 和 `History.vue -> refreshLogs()` (用于历史表格)

**请求参数:**
- `page`: 页码 (number, e.g., 1)
- `pageSize`: 每页数量 (number, e.g., 50)

**期望响应 (JSON):**

```json
{
  "total_count": 5000,  // 总日志条数 (用于分页控件)
  "logs": [
    {
      "trace_id": "uuid-123",
      "processed_at": "2023-10-27 10:05:00",
      "risk_level": "Critical", // 或 "High", "Error" 等，前端会进行映射
      "summary": "Detailed analysis result..."
    }
  ]
}
```

**前端过滤逻辑:**
- 目前 `History.vue` 中的搜索框 (`searchQuery`) 和等级筛选 (`filterLevel`) **尚未** 传递给后端。前端仅获取当前页数据后在客户端进行过滤。
- **建议:** 后端需支持 `?level=Critical&q=search_term` 参数，以便实现服务端过滤。

---

## 4. 设置 (Settings)

设置模块采用“整体加载，按需批量保存”的策略。

### 4.1 加载所有配置
- **接口地址:** `GET /api/settings/all`
- **前端调用位置:** `system.ts -> fetchSettings()`
- **描述:** 初始化时一次性拉取所有配置。

**期望响应 (JSON):**

```json
{
  "config": {
    "app_language": "zh",
    "log_retention_days": "7",
    "max_disk_usage_gb": "1",
    "http_port": "8080",
    "ai_provider": "OpenAI",
    "ai_model": "gpt-4",
    "ai_api_key": "sk-...",
    "ai_auto_degrade": "0",       // 0/1 或 true/false
    "ai_circuit_breaker": "1",
    "active_prompt_id": "0",
    "kernel_worker_threads": "4",
    // ... 其他键值对
  },
  "prompts": [
    {
      "id": 1,
      "name": "Security Audit",
      "content": "Analyze...",
      "is_active": 0
    }
  ],
  "channels": [
    {
      "id": 1,
      "name": "DingTalk",
      "provider": "DingTalk",
      "webhook_url": "https://...",
      "alert_threshold": "Error",
      "msg_template": "{...}",
      "is_active": 1
    }
  ]
}
```

### 4.2 保存基础配置 (Batch Config)
- **接口地址:** `POST /api/settings/config`
- **请求头:** `Content-Type: application/json`
- **描述:** 仅提交 Key-Value 类型的配置项（包括通用、AI参数、内核参数）。

**请求体:**
```json
{
  "items": [
    { "key": "app_language", "value": "zh" },
    { "key": "ai_provider", "value": "Azure" },
    { "key": "ai_auto_degrade", "value": "1" }
    // ... 仅包含变更项或全量提交
  ]
}
```

### 4.3 保存 Prompt 配置 (Batch Prompts)
- **接口地址:** `POST /api/settings/prompts`
- **描述:** 全量提交 Prompt 列表。ID 为 `0` 表示新增。

**请求体 (Array):**
```json
[
  {
    "id": 1,
    "name": "Audit",
    "content": "...",
    "is_active": 1
  },
  {
    "id": 0,          // 新增项
    "name": "New Prompt",
    "content": "...",
    "is_active": 0
  }
]
```

### 4.4 保存通知渠道 (Batch Channels)
- **接口地址:** `POST /api/settings/channels`
- **描述:** 全量提交渠道列表。

**请求体 (Array):**
```json
[
  {
    "id": 1,
    "name": "Ops Team",
    "provider": "DingTalk",
    "webhook_url": "...",
    "alert_threshold": "Critical",
    "msg_template": "...",
    "is_active": 1
  }
]
```

---

## 5. 系统操作

### 5.1 重启系统
- **接口地址:** `POST /api/settings/restart` (注意：`system.ts` 中使用的是 `/api/restart`，需确认后端路由)
- **前端实际代码:** `fetch('/api/restart', { method: 'POST' })`
- **描述:** 触发后端服务重启。前端会在收到成功响应 3 秒后刷新页面。

---

## 6. 类型映射参考 (Type Mapping)

前端 store (`system.ts`) 在接收数据时会进行类型宽容处理：
- **Boolean:** 后端返回 `1`, `"1"`, `true`, `"true"` 均会被解析为 `true`。
- **Risk Level:** 后端返回 `High` 会被映射为 `Critical` (展示为红色 RISK 标签)。
