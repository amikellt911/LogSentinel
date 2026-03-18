# Trace 读侧接口讨论稿（2026-03-17）

这个文档只记录当前讨论结论与待商榷点，后续以文档为准继续收口，不靠记忆推进。

## 当前暂定结论

### 1. 读侧先拆成两个接口

接口一：`POST /traces/search`

- 作用：查询 Trace 列表分页
- 返回：一页 trace summary
- 不返回：`spans`、`ai_analysis`、`prompt_debug`

接口二：`GET /traces/{trace_id}`

- 作用：查询单条 Trace 详情
- 返回：`summary + spans + ai_analysis`

这样拆的原因很直接：

- 列表页只需要摘要字段，没必要把整条调用链一起拉回来
- `spans` 数据量大，跟列表分页一起返回会把响应体做肥
- AI 分析和调用链通常只会在用户点开单条 trace 后才需要

### 2. 本轮先不做的内容

- 不做 `prompt_debug` 真联调
- 不做按耗时搜索/过滤（`min_duration/max_duration`）
- 只保留耗时展示，不把耗时作为首批检索条件

这部分已经同步记录到 `CurrentTask.md`。

## 当前建议的接口语义

### A. 列表接口只查摘要

当前收口结论：

- 列表里的 `service_name` 语义固定为“入口服务 / entry service”
- 第一版搜索里的 `service_name` 也只按入口服务筛选
- 第一版不做“任意参与服务筛选”
- 如果后续确实需要，再单独增加 `contains_service_name`

列表页第一版只需要这些核心字段：

- `trace_id`
- `service_name`
- `start_time_ms`
- `end_time_ms`
- `duration_ms`
- `span_count`
- `token_count`
- `risk_level`
- `page`
- `page_size`
- `total`

#### 列表搜索请求体（第一版）

```json
{
  "trace_id": "4",
  "service_name": "order-service",
  "start_time_ms": 1742200000000,
  "end_time_ms": 1742203600000,
  "risk_levels": ["Critical", "Error", "Warning"],
  "page": 1,
  "page_size": 20
}
```

说明：

- `trace_id` 非空时，按精确匹配处理；其他筛选条件可忽略
- `service_name` 当前语义固定为入口服务，不是“任意 span 所属服务”
- 前端的 `1h / 6h / 24h / custom` 只是 UI 交互，发给后端前统一换算成 `start_time_ms/end_time_ms`

#### 列表搜索响应体（第一版）

```json
{
  "page": 1,
  "page_size": 20,
  "total": 1,
  "data": [
    {
      "trace_id": "4",
      "service_name": "order-service",
      "start_time_ms": 1742200000000,
      "end_time_ms": 1742200000300,
      "duration_ms": 300,
      "span_count": 2,
      "token_count": 128,
      "risk_level": "Warning"
    }
  ]
}
```

补充约定：

- 列表接口不返回 `spans`
- 列表接口不返回 `ai_analysis`
- 列表接口不返回 `prompt_debug`
- `risk_level` 优先使用 AI 分析结果；若分析尚未落库，则回退为 summary 默认值

### B. 详情接口一次返回整条 Trace 的核心内容

详情页第一版只需要这些核心字段：

- summary：
  - `trace_id`
  - `service_name`
  - `start_time_ms`
  - `end_time_ms`
  - `duration_ms`
  - `span_count`
  - `token_count`
  - `risk_level`
- `spans`
- `ai_analysis`
- `tags`（见下方待商榷点）

#### 详情响应体（第一版）

```json
{
  "trace_id": "4",
  "service_name": "order-service",
  "start_time_ms": 1742200000000,
  "end_time_ms": 1742200000300,
  "duration_ms": 300,
  "span_count": 2,
  "token_count": 128,
  "risk_level": "Warning",
  "tags": [],
  "ai_analysis": {
    "risk_level": "Warning",
    "summary": "payment-service 响应波动导致下游请求放大。",
    "root_cause": "payment-service 高峰期超时，order-service 重试后拉长整条链路。",
    "solution": "先定位 payment-service 超时原因，再补限流和超时保护。",
    "confidence": 0.92
  },
  "spans": [
    {
      "span_id": "400",
      "parent_id": null,
      "service_name": "order-service",
      "operation": "root-op",
      "start_offset_ms": 0,
      "duration_ms": 300,
      "status": "success",
      "raw_status": "OK"
    },
    {
      "span_id": "401",
      "parent_id": "400",
      "service_name": "payment-service",
      "operation": "charge",
      "start_offset_ms": 100,
      "duration_ms": 100,
      "status": "error",
      "raw_status": "ERROR"
    }
  ]
}
```

补充约定：

- `ai_analysis` 允许为 `null`
- `tags` 当前允许先返回空数组，后续再切到 AI 真实生成
- `spans.start_offset_ms` 相对整条 trace 的开始时间，便于前端瀑布图直接消费
- `raw_status` 保留后端原始状态；`status` 给前端映射后的展示状态

## 待商榷点 1：tags 的来源与落点

当前结论：`tags` 不再使用前端 mock 生成，后续改为 AI 生成。

建议语义：

- `tags` 属于 AI 分析产物，而不是 Trace 原始主数据
- 既然它是 AI 产物，那么它应该跟 `ai_analysis` 一起返回
- 如果某条 trace 还没有分析结果，那么 `tags` 应为空数组，而不是前端瞎编

### 对实现的影响

如果后续确定让 AI proxy 返回标签数组，那么需要沿着这条链补字段：

- `ai/proxy` 返回结构增加 `tags: string[]`
- C++ `LogAnalysisResult` 增加 tags
- `TraceAnalysisRecord` 或单独持久化结构增加 tags 存储
- 详情查询接口把 tags 返回给前端

### 当前建议

本轮讨论阶段先把 `tags` 视为“AI 分析附属字段”，不要把它塞进列表接口的首批必需字段里。

原因很简单：

- 列表核心目标是快速扫摘要
- `tags` 依赖 AI 分析结果，不是每条 trace 在主数据落库时就天然可用
- 如果列表强依赖 tags，会把读侧首版接口复杂度抬高

## 待商榷点 2：service_name 的语义

这是当前最需要先讲清楚的点。

一个 trace 里可能有多个 span，而每个 span 可能来自不同服务。

所以“按服务筛选”不是不能做，而是必须先定义清楚：你筛的到底是哪一种服务。

### 方案 1：按入口服务 / 根服务筛选

语义：

- 列表里的 `service_name` 表示这条 trace 的入口服务（root span service）
- 搜索里的 `service_name` 也按这个字段筛

优点：

- 语义最稳定，跟列表展示一致
- 查询最便宜，直接用 `trace_summary.service_name`
- 当前库里已经有 `idx_trace_summary_service_name`

缺点：

- 如果用户想找“所有经过 payment-service 的 trace”，但 payment-service 只是中间 span，这种筛法查不到

### 方案 2：按“参与过的任意服务”筛选

语义：

- 只要某条 trace 的任意一个 span 命中 `service_name`，这条 trace 就算匹配

优点：

- 更贴近“我想看 payment-service 相关调用链”的排障习惯

缺点：

- 不能只查 `trace_summary`，必须查 `trace_span`
- SQL 通常会变成 `EXISTS (SELECT 1 FROM trace_span ... )`
- 列表里仍然不能只显示“命中的服务名”，因为一条 trace 可能有多个服务；展示字段和筛选字段会不是同一个概念

## 当前建议

第一版先把这两个概念拆开，不要混着叫一个 `service_name`：

- 列表展示字段：`entry_service_name`（或前端继续显示成 service_name，但文档语义明确它其实是入口服务）
- 可选的高级筛选字段：`contains_service_name`

如果本轮想先保守收口，那么建议：

- 第一版只做“入口服务筛选”
- 后面如果联调时发现排障价值不够，再追加 `contains_service_name`

这样做的原因是：

既然当前 TraceExplorer 首要目标是把读侧打通，那么第一版应该优先选择语义稳定、查询便宜、能直接落地的方案。

## 当前需要后续继续确认的问题

1. 列表字段是否只在文档层声明“`service_name` 实为入口服务”，还是直接改名为 `entry_service_name`
2. tags 是放进 `trace_analysis` 表扩字段，还是单独存 JSON
3. AI 结果未完成时，详情接口的 `ai_analysis` 返回 `null`、空对象还是失败态占位
4. 后端如何稳定定义入口服务：根 span 中最早开始的服务，还是沿用当前 summary 构建逻辑
