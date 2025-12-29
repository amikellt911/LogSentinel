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
        @row-click="handleRowClick"
        @ai-analysis="handleAIAnalysis"
        @call-chain="handleCallChain"
        @prompt-debug="handlePromptDebug"
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

    <!-- Prompt Debugger 抽屉 -->
    <PromptDebugger
      v-model="promptDrawerVisible"
      :prompt-debug-info="selectedPromptDebug"
      :trace-id="selectedTraceId"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import TraceSearchBar from '../components/TraceSearchBar.vue'
import TraceListTable from '../components/TraceListTable.vue'
import PromptDebugger from '../components/PromptDebugger.vue'
import AIAnalysisDrawerContent from '../components/AIAnalysisDrawerContent.vue'
import CallChainDrawerContent from '../components/CallChainDrawerContent.vue'
import type { TraceListItem, TraceDetail, TraceSearchCriteria, TraceSpan, AIAnalysis, PromptDebugInfo } from '../types/trace'
import dayjs from 'dayjs'

// 响应式状态
const loading = ref(false)
const traceList = ref<TraceListItem[]>([])
const total = ref(0)
const currentPage = ref(1)
const pageSize = ref(20)

// AI 分析抽屉状态
const aiDrawerVisible = ref(false)
const selectedTraceDetail = ref<TraceDetail | null>(null)
const selectedTraceId = ref('')

// 调用链抽屉状态
const callChainDrawerVisible = ref(false)

// Prompt Debugger 抽屉状态
const promptDrawerVisible = ref(false)
const selectedPromptDebug = ref<PromptDebugInfo | null>(null)

// TODO: Replace with real API
// TODO: 替换为真实 API 调用

/**
 * 生成 Mock Trace 列表数据
 *
 * 注意：列表显示的 duration 字段为"用户耗时"（业务耗时）
 * 定义：用户服务从请求开始到结束的真实墙钟时间
 * 计算：max(所有 span 的结束时间) - min(所有 span 的开始时间)
 * 不包含：LogSentinel 的分析耗时、等待超时时间
 */
function generateMockTraces(page: number, pageSize: number): { data: TraceListItem[]; total: number } {
  const mockData: TraceListItem[] = []
  const total = 500 // 模拟总数

  const services = ['user-service', 'order-service', 'payment-service', 'inventory-service', 'notification-service']
  const riskLevels: Array<'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe'> = ['Critical', 'Error', 'Warning', 'Info', 'Safe']

  for (let i = 0; i < pageSize; i++) {
    const index = (page - 1) * pageSize + i
    const r = Math.random()

    // 加权随机风险等级（偏向 Safe）
    let riskLevel: 'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe'
    if (r > 0.95) riskLevel = 'Critical'
    else if (r > 0.90) riskLevel = 'Error'
    else if (r > 0.80) riskLevel = 'Warning'
    else if (r > 0.60) riskLevel = 'Info'
    else riskLevel = 'Safe'

    // 模拟耗时（风险越高，耗时越长）
    let duration = Math.floor(Math.random() * 200) + 50
    if (riskLevel === 'Critical' || riskLevel === 'Error') duration += Math.random() * 1000
    if (riskLevel === 'Warning') duration += Math.random() * 500

    // 模拟 Span 数量
    const spanCount = Math.floor(Math.random() * 20) + 5

    // 模拟 Token 消耗（基于 Span 数量和风险等级）
    const tokenCount = Math.floor(spanCount * (100 + Math.random() * 50))

    mockData.push({
      trace_id: `trace_${String(1024 + index).padStart(6, '0')}`,
      service_name: services[Math.floor(Math.random() * services.length)],
      start_time: dayjs().subtract(Math.floor(Math.random() * 60), 'minute').format('YYYY-MM-DD HH:mm:ss'),
      duration: Math.floor(duration),
      span_count: spanCount,
      risk_level: riskLevel,
      token_count: tokenCount,
      timestamp: Date.now() - index * 60000
    })
  }

  return { data: mockData, total }
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

  // 生成 Prompt 调试信息
  const promptDebug: PromptDebugInfo = {
    trace_id: traceId,
    input: {
      trace_context: {
        trace_id: traceId,
        service_name: services[0],
        span_count: spanCount,
        total_duration: currentTime,
        risk_spans: mockSpans.filter(s => s.status === 'error').map(s => s.span_id)
      },
      constraint: '分析此 Trace 的风险等级，提供根因分析和解决建议。',
      system_prompt: '你是一个专业的日志分析助手，擅长识别分布式系统中的异常和性能瓶颈。'
    },
    output: {
      risk_level: aiAnalysis.risk_level,
      summary: aiAnalysis.summary,
      root_cause: aiAnalysis.root_cause,
      solution: aiAnalysis.solution
    },
    metadata: {
      timestamp: dayjs().format('YYYY-MM-DD HH:mm:ss'),
      model: 'gemini-2.0-flash-exp',
      duration: Math.floor(Math.random() * 500) + 100,
      total_tokens: tokenCount
    }
  }

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
    ai_analysis: aiAnalysis,
    prompt_debug: promptDebug
  }
}

/**
 * 处理搜索
 */
function handleSearch(criteria: TraceSearchCriteria) {
  console.log('搜索条件:', criteria)
  loading.value = true

  // TODO: 替换为真实 API 调用
  // 模拟异步加载
  setTimeout(() => {
    const result = generateMockTraces(currentPage.value, pageSize.value)
    traceList.value = result.data
    total.value = result.total
    loading.value = false
  }, 600)
}

/**
 * 处理重置
 */
function handleReset() {
  currentPage.value = 1
  loadTraces()
}

/**
 * 加载 Trace 列表
 */
function loadTraces() {
  loading.value = true

  // TODO: 替换为真实 API 调用
  setTimeout(() => {
    const result = generateMockTraces(currentPage.value, pageSize.value)
    traceList.value = result.data
    total.value = result.total
    loading.value = false
  }, 600)
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
 * Prompt Debugger 按钮点击事件
 */
function handlePromptDebug(row: TraceListItem) {
  // TODO: 替换为真实 API 调用
  const traceDetail = generateMockTraceDetail(row.trace_id)
  selectedPromptDebug.value = traceDetail.prompt_debug
  selectedTraceId.value = row.trace_id
  promptDrawerVisible.value = true
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
  loadTraces()
})
</script>

<style scoped>
/* 确保页面占满全屏 */
</style>
