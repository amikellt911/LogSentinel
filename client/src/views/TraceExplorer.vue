<template>
  <div class="h-full flex flex-col p-6 space-y-4 overflow-hidden">
    <!-- 页面标题 -->
    <div class="flex items-center gap-3">
      <h1 class="text-2xl font-bold text-gray-100">
        {{ $t('layout.traceExplorer') }}
      </h1>
    </div>

    <!-- 顶部：搜索表单 -->
    <TraceSearchBar
      :loading="loading"
      @search="handleSearch"
      @reset="handleReset"
    />

    <!-- 底部：Trace 列表 + 详情抽屉 -->
    <div class="flex-1 overflow-hidden">
      <TraceListTable
        :data="traceList"
        :loading="loading"
        :total="total"
        :current-page="currentPage"
        :page-size="pageSize"
        @row-click="handleRowClick"
        @ai-analysis="handleAIAnalysis"
        @call-chain="handleCallChain"
        @page-change="handlePageChange"
        @size-change="handleSizeChange"
      />
    </div>

    <!-- AI 分析抽屉 -->
    <el-drawer
      v-model="aiDrawerVisible"
      :title="$t('traceExplorer.table.aiAnalysis') + ' - ' + selectedTraceId"
      direction="rtl"
      size="70%"
      :destroy-on-close="true"
      @closed="handleAIDrawerClosed"
    >
      <AIAnalysisDrawerContent
        v-if="selectedTraceDetail"
        :trace-detail="selectedTraceDetail"
      />
    </el-drawer>

    <!-- 调用链抽屉 -->
    <el-drawer
      v-model="callChainDrawerVisible"
      :title="$t('traceExplorer.table.callChain') + ' - ' + selectedTraceId"
      direction="rtl"
      size="70%"
      :destroy-on-close="true"
      @closed="handleCallChainDrawerClosed"
    >
      <CallChainDrawerContent
        v-if="selectedTraceDetail"
        :trace-detail="selectedTraceDetail"
      />
    </el-drawer>

  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import TraceSearchBar from '../components/TraceSearchBar.vue'
import TraceListTable from '../components/TraceListTable.vue'
import AIAnalysisDrawerContent from '../components/AIAnalysisDrawerContent.vue'
import CallChainDrawerContent from '../components/CallChainDrawerContent.vue'
import type {
  TraceListItem,
  TraceDetail,
  TraceSearchCriteria,
  TraceSpan,
  AIAnalysis,
  RiskLevel,
  TraceSearchRequestPayload,
  TraceSearchResponseDto,
  TraceListItemDto
} from '../types/trace'
import dayjs from 'dayjs'

// 响应式状态
const loading = ref(false)
const traceList = ref<TraceListItem[]>([])
const total = ref(0)
const currentPage = ref(1)
const pageSize = ref(20)
const currentSearchCriteria = ref<TraceSearchCriteria>(createDefaultSearchCriteria())

// AI 分析抽屉状态
const aiDrawerVisible = ref(false)
const selectedTraceDetail = ref<TraceDetail | null>(null)
const selectedTraceId = ref('')

// 调用链抽屉状态
const callChainDrawerVisible = ref(false)

function createDefaultSearchCriteria(): TraceSearchCriteria {
  return {
    trace_id: '',
    service_name: '',
    time_range: '24h',
    risk_level: []
  }
}

function formatTraceListTime(startTimeMs: number): string {
  return dayjs(startTimeMs).format('YYYY-MM-DD HH:mm:ss')
}

function normalizeRiskLevel(level: string): RiskLevel {
  const normalized = level.trim().toLowerCase()
  switch (normalized) {
    case 'critical':
      return 'Critical'
    case 'error':
      return 'Error'
    case 'warning':
      return 'Warning'
    case 'info':
      return 'Info'
    case 'safe':
      return 'Safe'
    default:
      return 'Unknown'
  }
}

function mapTraceListItem(dto: TraceListItemDto): TraceListItem {
  return {
    trace_id: dto.trace_id,
    service_name: dto.service_name,
    start_time: formatTraceListTime(dto.start_time_ms),
    duration: dto.duration_ms,
    span_count: dto.span_count,
    risk_level: normalizeRiskLevel(dto.risk_level),
    token_count: dto.token_count,
    timestamp: dto.start_time_ms
  }
}

function buildTraceSearchPayload(criteria: TraceSearchCriteria): TraceSearchRequestPayload | null {
  const payload: TraceSearchRequestPayload = {
    page: currentPage.value,
    page_size: pageSize.value
  }

  const traceId = criteria.trace_id?.trim() ?? ''
  if (traceId) {
    payload.trace_id = traceId
    return payload
  }

  const serviceName = criteria.service_name?.trim() ?? ''
  if (serviceName) {
    payload.service_name = serviceName
  }

  const riskLevels = criteria.risk_level ?? []
  if (riskLevels.length > 0) {
    payload.risk_levels = riskLevels.map(level => level.toLowerCase())
  }

  const timeRange = criteria.time_range ?? '24h'
  if (timeRange === 'custom') {
    if (!criteria.custom_time_start || !criteria.custom_time_end) {
      ElMessage.warning('自定义时间范围需要同时填写开始时间和结束时间')
      return null
    }
    const startTime = dayjs(criteria.custom_time_start)
    const endTime = dayjs(criteria.custom_time_end)
    if (!startTime.isValid() || !endTime.isValid()) {
      ElMessage.warning('自定义时间格式无效')
      return null
    }
    if (endTime.valueOf() <= startTime.valueOf()) {
      ElMessage.warning('结束时间必须晚于开始时间')
      return null
    }
    payload.start_time_ms = startTime.valueOf()
    payload.end_time_ms = endTime.valueOf()
    return payload
  }

  const endTimeMs = Date.now()
  const rangeHoursMap: Record<'1h' | '6h' | '24h', number> = {
    '1h': 1,
    '6h': 6,
    '24h': 24
  }
  const hours = rangeHoursMap[timeRange as '1h' | '6h' | '24h'] ?? 24
  payload.start_time_ms = endTimeMs - hours * 60 * 60 * 1000
  payload.end_time_ms = endTimeMs
  return payload
}

async function fetchTraceList() {
  const payload = buildTraceSearchPayload(currentSearchCriteria.value)
  if (!payload) {
    return
  }

  loading.value = true
  try {
    const response = await fetch('/api/traces/search', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(payload)
    })

    if (!response.ok) {
      let errorMessage = `Trace 列表查询失败（HTTP ${response.status}）`
      try {
        const errorBody = await response.json()
        if (typeof errorBody?.error === 'string' && errorBody.error.length > 0) {
          errorMessage = errorBody.error
        }
      } catch {
        // 这里吞掉 JSON 解析失败，继续使用默认错误文案。
      }
      throw new Error(errorMessage)
    }

    const result: TraceSearchResponseDto = await response.json()
    traceList.value = result.items.map(mapTraceListItem)
    total.value = result.total
  } catch (error) {
    console.error('Trace 列表查询失败:', error)
    traceList.value = []
    total.value = 0
    ElMessage.error(error instanceof Error ? error.message : 'Trace 列表查询失败')
  } finally {
    loading.value = false
  }
}

/**
 * 生成 Mock Trace 详情数据
 *
 * TraceDetail.duration 字段说明：
 * - 计算：最后一个 Span 结束时间 - 第一个 Span 开始时间
 * - 含义：用户业务的实际墙钟时间（不包含 LogSentinel 分析耗时）
 * - 方式：动态计算，每次新 Span 到达时更新（流式更新）
 */
function generateMockTraceDetail(traceId: string): TraceDetail {
  const mockSpans: TraceSpan[] = []
  const services = ['user-service', 'order-service', 'payment-service', 'inventory-service', 'notification-service']
  const operations = [
    'GET /api/v1/user',
    'POST /api/v1/order',
    'POST /api/v1/payment',
    'GET /api/v1/inventory',
    'POST /api/v1/notification'
  ]

  // 生成 5-15 个 Span
  const spanCount = Math.floor(Math.random() * 10) + 5
  let currentTime = 0

  for (let i = 0; i < spanCount; i++) {
    const duration = Math.floor(Math.random() * 100) + 20
    const status = Math.random() > 0.9 ? 'error' : (Math.random() > 0.8 ? 'warning' : 'success')

    mockSpans.push({
      span_id: `span_${i + 1}`,
      service_name: services[i % services.length],
      start_time: currentTime,
      duration: duration,
      parent_id: i > 0 ? `span_${i}` : null,
      status: status,
      operation: operations[i % operations.length]
    })

    currentTime += duration + Math.floor(Math.random() * 20) // 添加一些延迟
  }

  // 生成 AI 分析结果
  const hasCritical = mockSpans.some(s => s.status === 'error')
  const aiAnalysis: AIAnalysis = hasCritical ? {
    risk_level: 'Critical',
    summary: '检测到多个服务调用失败，导致整个 Trace 失败。',
    root_cause: 'payment-service 响应超时，导致下游服务连锁失败。',
    solution: '建议：1) 检查 payment-service 性能；2) 增加超时重试机制；3) 添加熔断器保护。',
    confidence: 0.92
  } : {
    risk_level: 'Safe',
    summary: 'Trace 执行正常，所有服务调用成功。',
    root_cause: '无异常。',
    solution: '保持当前配置。',
    confidence: 0.98
  }

  // 生成标签
  const allTags = ['认证', '安全', 'SQL注入', '登录', '数据库', '性能', '内存', '网络', '异常', '超时']
  const tagCount = Math.floor(Math.random() * 3) + 1 // 1-3 个标签
  const tags: string[] = []
  for (let i = 0; i < tagCount; i++) {
    const tag = allTags[Math.floor(Math.random() * allTags.length)]
    if (!tags.includes(tag)) {
      tags.push(tag)
    }
  }

  // Token 消耗
  const tokenCount = Math.floor(spanCount * (100 + Math.random() * 50))

  return {
    trace_id: traceId,
    service_name: services[0],
    start_time: dayjs().format('YYYY-MM-DD HH:mm:ss'),
    end_time: dayjs().add(currentTime, 'ms').format('YYYY-MM-DD HH:mm:ss'),
    duration: currentTime,
    span_count: spanCount,
    token_count: tokenCount,
    tags: tags,
    spans: mockSpans,
    ai_analysis: aiAnalysis
  }
}

/**
 * 处理搜索
 */
function handleSearch(criteria: TraceSearchCriteria) {
  const nextCriteria: TraceSearchCriteria = {
    ...createDefaultSearchCriteria(),
    ...criteria,
    risk_level: criteria.risk_level ? [...criteria.risk_level] : []
  }
  currentPage.value = 1
  if (!buildTraceSearchPayload(nextCriteria)) {
    return
  }
  currentSearchCriteria.value = nextCriteria
  void fetchTraceList()
}

/**
 * 处理重置
 */
function handleReset() {
  currentSearchCriteria.value = createDefaultSearchCriteria()
  currentPage.value = 1
  void fetchTraceList()
}

/**
 * 加载 Trace 列表
 */
function loadTraces() {
  void fetchTraceList()
}

/**
 * 行点击事件（默认打开 AI 分析）
 */
function handleRowClick(row: TraceListItem) {
  handleAIAnalysis(row)
}

/**
 * AI 分析按钮点击事件
 */
function handleAIAnalysis(row: TraceListItem) {
  // TODO: 替换为真实 API 调用
  const traceDetail = generateMockTraceDetail(row.trace_id)
  selectedTraceDetail.value = traceDetail
  selectedTraceId.value = row.trace_id
  aiDrawerVisible.value = true
}

/**
 * 调用链按钮点击事件
 */
function handleCallChain(row: TraceListItem) {
  // TODO: 替换为真实 API 调用
  const traceDetail = generateMockTraceDetail(row.trace_id)
  selectedTraceDetail.value = traceDetail
  selectedTraceId.value = row.trace_id
  callChainDrawerVisible.value = true
}

/**
 * AI 分析抽屉关闭时清理数据
 */
function handleAIDrawerClosed() {
  selectedTraceDetail.value = null
  selectedTraceId.value = ''
}

/**
 * 调用链抽屉关闭时清理数据
 */
function handleCallChainDrawerClosed() {
  selectedTraceDetail.value = null
  selectedTraceId.value = ''
}

/**
 * 页码变化
 */
function handlePageChange(page: number) {
  currentPage.value = page
  loadTraces()
}

/**
 * 每页数量变化
 */
function handleSizeChange(size: number) {
  pageSize.value = size
  currentPage.value = 1
  loadTraces()
}

onMounted(() => {
  currentSearchCriteria.value = createDefaultSearchCriteria()
  loadTraces()
})
</script>

<style scoped>
/* 确保页面占满全屏 */
</style>
