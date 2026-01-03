<template>
  <div class="h-full flex flex-col p-6 space-y-6 overflow-hidden relative bg-black/20">
    <!-- Header Section -->
    <div class="flex justify-between items-center">
      <div>
        <h2 class="text-xl font-bold text-gray-100 flex items-center gap-3">
          <el-icon class="text-purple-400"><Cpu /></el-icon>
          {{ $t('aiEngine.title') }}
        </h2>
        <p class="text-gray-500 text-sm mt-1 font-mono">
          {{ $t('aiEngine.subtitle') }}
        </p>
      </div>
      <div class="flex items-center gap-4">
        <div class="flex items-center gap-2 text-xs font-mono bg-gray-900 px-3 py-1 rounded border border-gray-700">
          <span class="w-2 h-2 rounded-full bg-green-500 animate-pulse"></span>
          {{ $t('aiEngine.status') }}: {{ $t('aiEngine.running') }}
        </div>
        <el-tag effect="dark" type="info" class="font-mono">{{ $t('aiEngine.model') }}: Gemini Pro</el-tag>
      </div>
    </div>

    <!-- Token 指标卡片 -->
    <TokenMetricsCard />

    <!-- 搜索栏 -->
    <AIEngineSearchBar
      :loading="loading"
      @search="handleSearch"
      @reset="handleReset"
    />

    <!-- 历史批次存档列表 -->
    <div class="flex-1 flex flex-col min-h-0">
      <div class="flex items-center justify-between mb-3">
        <h3 class="text-sm font-bold text-gray-300 flex items-center gap-2">
          <el-icon class="text-blue-400"><FolderOpened /></el-icon>
          {{ $t('aiEngine.batchArchive') }}
        </h3>
      </div>

      <div class="flex-1 overflow-auto">
        <BatchArchiveList @open-prompt-debugger="handleOpenPromptDebugger" />
      </div>
    </div>

    <!-- Prompt 透视抽屉 -->
    <PromptDebugger
      v-model:visible="promptDebuggerVisible"
      :debug-info="currentDebugInfo"
    />
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { Cpu, FolderOpened } from '@element-plus/icons-vue'
import TokenMetricsCard from '../components/TokenMetricsCard.vue'
import AIEngineSearchBar, { AIEngineSearchCriteria } from '../components/AIEngineSearchBar.vue'
import BatchArchiveList from '../components/BatchArchiveList.vue'
import PromptDebugger from '../components/PromptDebugger.vue'

// TODO: Replace with real API
/**
 * Prompt 调试信息数据结构（与 PromptDebugger.vue 保持一致）
 */
interface PromptDebugInfo {
  trace_id: string
  model: string
  duration: number
  totalTokens: number
  timestamp: string
  input: {
    trace_context: any
    constraint: string
    system_prompt: string
  }
  output: {
    risk_level: string
    summary: string
    root_cause: string
    solution: string
  }
}

// 加载状态
const loading = ref(false)

// Prompt 透视抽屉
const promptDebuggerVisible = ref(false)
const currentDebugInfo = ref<PromptDebugInfo | null>(null)

/**
 * 处理搜索
 */
function handleSearch(criteria: AIEngineSearchCriteria) {
  console.log('AI Engine 搜索条件:', criteria)
  loading.value = true

  // TODO: 替换为真实 API 调用
  // 模拟异步加载
  setTimeout(() => {
    loading.value = false
    // 这里应该调用 BatchArchiveList 的搜索方法
    // 目前暂未实现搜索逻辑
  }, 500)
}

/**
 * 处理重置
 */
function handleReset() {
  console.log('AI Engine 重置搜索')
  // TODO: 重置 BatchArchiveList 的数据
}

/**
 * 打开 Prompt 透视抽屉
 */
function handleOpenPromptDebugger(batch: any) {
  // TODO: 从 API 获取详细的 Prompt 调试信息
  currentDebugInfo.value = {
    trace_id: batch.trace_id,
    model: 'gemini-pro',
    duration: Math.floor(Math.random() * 500) + 100,
    totalTokens: batch.token_count,
    timestamp: batch.created_at,
    input: {
      trace_context: {
        trace_id: batch.trace_id,
        logs: generateMockLogs(batch.log_count),
        metadata: {
          service: 'log-sentinel',
          environment: 'production'
        }
      },
      constraint: 'Analyze the following logs and provide root cause analysis.',
      system_prompt: 'You are an expert log analysis system. Identify anomalies, risks, and provide actionable recommendations.'
    },
    output: {
      risk_level: batch.risk_level,
      summary: batch.summary,
      root_cause: 'Detected elevated error rates in authentication service due to database connection timeout.',
      solution: '1. Check database connection pool settings.\n2. Increase connection timeout threshold.\n3. Monitor database query performance.'
    }
  }
  promptDebuggerVisible.value = true
}

/**
 * 生成 Mock 日志数据（用于演示）
 */
function generateMockLogs(count: number): any[] {
  const logs = []
  const templates = [
    { level: 'ERROR', message: 'Database connection timeout' },
    { level: 'WARN', message: 'High memory usage detected' },
    { level: 'INFO', message: 'User login successful' },
    { level: 'ERROR', message: 'SQL injection attempt blocked' }
  ]

  for (let i = 0; i < Math.min(count, 5); i++) {
    const tpl = templates[Math.floor(Math.random() * templates.length)]
    logs.push({
      timestamp: new Date(Date.now() - i * 1000).toISOString(),
      level: tpl.level,
      message: tpl.message,
      service: 'auth-service'
    })
  }

  return logs
}
</script>

<style scoped>
/* 无额外样式，使用 Tailwind CSS */
</style>
