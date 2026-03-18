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
        @view-detail="handleViewDetail"
        @page-change="handlePageChange"
        @size-change="handleSizeChange"
      />
    </div>

    <!-- 详情抽屉 -->
    <el-drawer
      v-model="detailDrawerVisible"
      :title="$t('traceExplorer.table.viewDetails') + ' - ' + selectedTraceId"
      direction="rtl"
      size="70%"
      :destroy-on-close="true"
      @closed="handleDetailDrawerClosed"
    >
      <div
        class="h-full flex flex-col gap-6 overflow-y-auto"
        v-loading="detailLoading"
        element-loading-background="rgba(0, 0, 0, 0.35)"
      >
        <AIAnalysisDrawerContent
          v-if="selectedTraceDetail"
          :trace-detail="selectedTraceDetail"
        />
        <div
          v-if="selectedTraceDetail"
          class="border-t border-gray-700/80 pt-6 min-h-[520px]"
        >
          <CallChainDrawerContent
            :trace-detail="selectedTraceDetail"
          />
        </div>
      </div>
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
  RiskLevel,
  TraceSearchRequestPayload,
  TraceSearchResponseDto,
  TraceListItemDto,
  TraceDetailResponseDto,
  TraceSpanDto,
  TraceAnalysisDto
} from '../types/trace'
import dayjs from 'dayjs'

// 响应式状态
const loading = ref(false)
const traceList = ref<TraceListItem[]>([])
const total = ref(0)
const currentPage = ref(1)
const pageSize = ref(20)
const currentSearchCriteria = ref<TraceSearchCriteria>(createDefaultSearchCriteria())

// 详情抽屉状态
const detailDrawerVisible = ref(false)
const detailLoading = ref(false)
const selectedTraceDetail = ref<TraceDetail | null>(null)
const selectedTraceId = ref('')
let detailRequestSeq = 0

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

function formatTraceDetailTime(timeMs: number | null): string {
  if (timeMs === null) {
    return ''
  }
  return dayjs(timeMs).format('YYYY-MM-DD HH:mm:ss')
}

function mapSpanStatus(rawStatus: string): 'success' | 'error' | 'warning' {
  const normalized = rawStatus.trim().toLowerCase()
  if (normalized === 'ok') {
    return 'success'
  }
  if (normalized === 'error') {
    return 'error'
  }
  return 'warning'
}

function mapTraceAnalysis(dto: TraceAnalysisDto | null) {
  if (!dto) {
    return null
  }
  return {
    risk_level: normalizeRiskLevel(dto.risk_level),
    summary: dto.summary,
    root_cause: dto.root_cause,
    solution: dto.solution,
    confidence: dto.confidence
  }
}

function mapTraceSpan(dto: TraceSpanDto, traceStartTimeMs: number) {
  return {
    span_id: dto.span_id,
    service_name: dto.service_name,
    start_time: Math.max(0, dto.start_time_ms - traceStartTimeMs),
    duration: dto.duration_ms,
    parent_id: dto.parent_id,
    status: mapSpanStatus(dto.raw_status),
    operation: dto.operation
  }
}

function mapTraceDetail(dto: TraceDetailResponseDto): TraceDetail {
  return {
    trace_id: dto.trace_id,
    service_name: dto.service_name,
    start_time: formatTraceDetailTime(dto.start_time_ms),
    end_time: formatTraceDetailTime(dto.end_time_ms),
    duration: dto.duration_ms,
    span_count: dto.span_count,
    token_count: dto.token_count,
    tags: Array.isArray(dto.tags) ? dto.tags : [],
    spans: dto.spans.map(span => mapTraceSpan(span, dto.start_time_ms)),
    ai_analysis: mapTraceAnalysis(dto.analysis)
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

async function fetchTraceDetail(traceId: string): Promise<TraceDetail> {
  const response = await fetch(`/api/traces/${encodeURIComponent(traceId)}`)
  if (!response.ok) {
    let errorMessage = `Trace 详情查询失败（HTTP ${response.status}）`
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

  const result: TraceDetailResponseDto = await response.json()
  return mapTraceDetail(result)
}

async function ensureTraceDetail(traceId: string): Promise<boolean> {
  if (selectedTraceDetail.value && selectedTraceId.value === traceId) {
    return true
  }

  const requestSeq = ++detailRequestSeq
  detailLoading.value = true
  selectedTraceId.value = traceId
  selectedTraceDetail.value = null

  try {
    const detail = await fetchTraceDetail(traceId)
    if (requestSeq !== detailRequestSeq) {
      return false
    }
    selectedTraceDetail.value = detail
    return true
  } catch (error) {
    if (requestSeq === detailRequestSeq) {
      selectedTraceDetail.value = null
      ElMessage.error(error instanceof Error ? error.message : 'Trace 详情查询失败')
    }
    return false
  } finally {
    if (requestSeq === detailRequestSeq) {
      detailLoading.value = false
    }
  }
}

function clearSelectedTraceDetailIfAllDrawersClosed() {
  if (!detailDrawerVisible.value) {
    selectedTraceDetail.value = null
    selectedTraceId.value = ''
    detailLoading.value = false
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
 * 行点击事件（默认打开详情）
 */
function handleRowClick(row: TraceListItem) {
  void handleViewDetail(row)
}

async function handleViewDetail(row: TraceListItem) {
  detailDrawerVisible.value = true
  const ok = await ensureTraceDetail(row.trace_id)
  if (!ok) {
    detailDrawerVisible.value = false
    clearSelectedTraceDetailIfAllDrawersClosed()
  }
}

/**
 * 详情抽屉关闭时清理数据
 */
function handleDetailDrawerClosed() {
  clearSelectedTraceDetailIfAllDrawersClosed()
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
