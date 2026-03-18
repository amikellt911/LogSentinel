/**
 * Trace 相关的 TypeScript 类型定义
 * 用于 TraceExplorer 页面和相关组件
 */

export type RiskLevel = 'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe' | 'Unknown'

/**
 * Trace Span 节点（调用链中的单个操作）
 */
export interface TraceSpan {
  span_id: string // Span ID（唯一标识符）
  service_name: string // 服务名称（如 "user-service", "order-service"）
  start_time: number // 开始时间（相对时间，ms）
  duration: number // 持续时间（ms）
  parent_id: string | null // 父 Span ID（null 表示根 Span）
  status: 'success' | 'error' | 'warning' // 状态
  operation?: string // 操作名称（如 "GET /api/v1/user"）
}

/**
 * AI 分析结果
 */
export interface AIAnalysis {
  risk_level: RiskLevel // 风险等级
  summary: string // 根因总结
  root_cause: string // 根因分析
  solution: string // 解决建议
  confidence: number // 置信度（0-1）
}

/**
 * Trace 列表项（显示在表格中）
 */
export interface TraceListItem {
  trace_id: string // Trace ID（等同于 batch_id）
  service_name: string // 主要服务名称
  start_time: string // 开始时间（格式化后的字符串，如 "2025-12-29 14:30:25"）
  duration: number // 用户耗时（ms），即业务系统从请求开始到结束的真实墙钟时间
  span_count: number // Span 数量（调用链中的操作节点数）
  risk_level: RiskLevel // 风险等级
  token_count: number // Token 消耗数量
  timestamp: number // 时间戳（用于排序）
}

/**
 * Trace 列表查询请求（后端 DTO）
 */
export interface TraceSearchRequestPayload {
  trace_id?: string // Trace ID 精确匹配
  service_name?: string // 入口服务精确匹配
  start_time_ms?: number // 搜索窗口起点（毫秒时间戳）
  end_time_ms?: number // 搜索窗口终点（毫秒时间戳）
  risk_levels?: string[] // 风险等级数组（按后端存储值传递）
  page: number // 页码，从 1 开始
  page_size: number // 每页条数
}

/**
 * Trace 列表项（后端 DTO）
 */
export interface TraceListItemDto {
  trace_id: string
  service_name: string
  start_time_ms: number
  end_time_ms: number | null
  duration_ms: number
  span_count: number
  token_count: number
  risk_level: string
}

/**
 * Trace 列表查询响应（后端 DTO）
 */
export interface TraceSearchResponseDto {
  total: number
  items: TraceListItemDto[]
}

/**
 * Trace 详情里的单个 Span（后端 DTO）
 */
export interface TraceSpanDto {
  span_id: string
  parent_id: string | null
  service_name: string
  operation: string
  start_time_ms: number
  duration_ms: number
  raw_status: string
}

/**
 * Trace 详情里的 AI 分析（后端 DTO）
 */
export interface TraceAnalysisDto {
  risk_level: string
  summary: string
  root_cause: string
  solution: string
  confidence: number
}

/**
 * Trace 详情查询响应（后端 DTO）
 */
export interface TraceDetailResponseDto {
  trace_id: string
  service_name: string
  start_time_ms: number
  end_time_ms: number | null
  duration_ms: number
  span_count: number
  token_count: number
  risk_level: string
  tags: string[]
  spans: TraceSpanDto[]
  analysis: TraceAnalysisDto | null
}

/**
 * Prompt 调试信息（用于 Prompt Debugger）
 */
export interface PromptDebugInfo {
  trace_id: string // Trace ID
  input: {
    trace_context: Record<string, any> // 完整的追踪上下文
    constraint: string // 分析约束
    system_prompt: string // 系统提示词
  }
  output: {
    risk_level: string // 风险等级
    summary: string // 总结
    root_cause: string // 根因
    solution: string // 解决方案
  }
  metadata: {
    timestamp: string // 时间戳
    model: string // 模型名称
    duration: number // 耗时（ms）
    total_tokens: number // 总 Token 数
  }
}

/**
 * Trace 详情数据（用于详情抽屉）
 */
export interface TraceDetail {
  trace_id: string // Trace ID
  service_name: string // 主要服务名称
  start_time: string // 开始时间（格式化）
  end_time: string // 结束时间（格式化）
  duration: number // 用户耗时（ms），即业务系统从请求开始到结束的真实墙钟时间
  span_count: number // Span 数量
  token_count: number // Token 消耗数量
  tags: string[] // 标签（如 ['认证', '安全']）
  spans: TraceSpan[] // Span 列表（调用链）
  ai_analysis: AIAnalysis | null // AI 分析结果
}

/**
 * 搜索条件
 */
export interface TraceSearchCriteria {
  trace_id?: string // Trace ID（精确匹配）
  service_name?: string // 入口服务名称（精确匹配）
  time_range?: '1h' | '6h' | '24h' | 'custom' // 时间范围
  custom_time_start?: string // 自定义开始时间
  custom_time_end?: string // 自定义结束时间
  risk_level?: RiskLevel[] // 风险等级（多选）
}

/**
 * 分页参数
 */
export interface PaginationParams {
  page: number // 当前页码
  page_size: number // 每页数量
  total: number // 总数
}

/**
 * 排序参数
 */
export interface SortParams {
  field: 'start_time' | 'duration' | 'risk_level' // 排序字段
  order: 'asc' | 'desc' // 排序方向
}
