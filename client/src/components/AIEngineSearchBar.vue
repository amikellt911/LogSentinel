<template>
  <div class="bg-gray-800/30 p-4 rounded border border-gray-700">
    <div class="flex flex-wrap gap-4 items-end">
      <!-- Trace ID 搜索 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('aiEngine.traceId') }}</label>
        <el-input
          v-model="searchCriteria.trace_id"
          :placeholder="$t('aiEngine.traceIdPlaceholder')"
          size="small"
          clearable
          class="w-48"
          @keyup.enter="handleSearch"
        />
      </div>

      <!-- 时间范围选择 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('aiEngine.timeRange') }}</label>
        <el-select
          v-model="searchCriteria.time_range"
          size="small"
          class="w-32"
          @change="handleTimeRangeChange"
        >
          <el-option :label="$t('aiEngine.last1h')" value="1h" />
          <el-option :label="$t('aiEngine.last6h')" value="6h" />
          <el-option :label="$t('aiEngine.last24h')" value="24h" />
          <el-option :label="$t('aiEngine.last7d')" value="7d" />
          <el-option :label="$t('aiEngine.customTime')" value="custom" />
        </el-select>
      </div>

      <!-- 风险等级选择 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('aiEngine.riskLevel') }}</label>
        <el-select
          v-model="searchCriteria.risk_level"
          size="small"
          clearable
          class="w-32"
        >
          <el-option :label="$t('aiEngine.allRisks')" value="all" />
          <el-option label="Critical" value="critical" />
          <el-option label="Error" value="error" />
          <el-option label="Warning" value="warning" />
          <el-option label="Info" value="info" />
          <el-option label="Safe" value="safe" />
        </el-select>
      </div>

      <!-- 自定义时间范围（条件渲染） -->
      <div v-if="searchCriteria.time_range === 'custom'" class="flex gap-2 items-end">
        <el-date-picker
          v-model="customTimeStart"
          type="datetime"
          :placeholder="$t('traceExplorer.search.startTime')"
          size="small"
          format="YYYY-MM-DD HH:mm:ss"
          value-format="YYYY-MM-DD HH:mm:ss"
        />
        <span class="text-gray-500">~</span>
        <el-date-picker
          v-model="customTimeEnd"
          type="datetime"
          :placeholder="$t('traceExplorer.search.endTime')"
          size="small"
          format="YYYY-MM-DD HH:mm:ss"
          value-format="YYYY-MM-DD HH:mm:ss"
        />
      </div>

      <!-- 操作按钮 -->
      <div class="flex gap-2 ml-auto">
        <el-button size="small" @click="handleReset">
          {{ $t('traceExplorer.search.reset') }}
        </el-button>
        <el-button type="primary" size="small" @click="handleSearch" :loading="loading">
          <el-icon class="mr-1"><Search /></el-icon>
          {{ $t('traceExplorer.search.search') }}
        </el-button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref, watch } from 'vue'
import { Search } from '@element-plus/icons-vue'

// TODO: Replace with real API
/**
 * AI Engine 搜索条件数据结构
 */
export interface AIEngineSearchCriteria {
  trace_id: string
  time_range: '1h' | '6h' | '24h' | '7d' | 'custom'
  risk_level: 'all' | 'critical' | 'error' | 'warning' | 'info' | 'safe' | ''
  custom_time_start?: string
  custom_time_end?: string
}

// Props
interface Props {
  loading?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  loading: false
})

// Emits
const emit = defineEmits<{
  search: [criteria: AIEngineSearchCriteria]
  reset: []
}>()

// 搜索条件
const searchCriteria = reactive<AIEngineSearchCriteria>({
  trace_id: '',
  time_range: '24h',
  risk_level: 'all'
})

// 自定义时间范围
const customTimeStart = ref<string>('')
const customTimeEnd = ref<string>('')

/**
 * 处理搜索
 */
function handleSearch() {
  const criteria = { ...searchCriteria }
  // 如果选择自定义时间范围，添加自定义时间
  if (criteria.time_range === 'custom') {
    criteria.custom_time_start = customTimeStart.value
    criteria.custom_time_end = customTimeEnd.value
  }
  emit('search', criteria)
}

/**
 * 处理重置
 */
function handleReset() {
  searchCriteria.trace_id = ''
  searchCriteria.time_range = '24h'
  searchCriteria.risk_level = 'all'
  customTimeStart.value = ''
  customTimeEnd.value = ''
  emit('reset')
}

/**
 * 处理时间范围变化
 */
function handleTimeRangeChange(value: string) {
  if (value !== 'custom') {
    customTimeStart.value = ''
    customTimeEnd.value = ''
  }
}

/**
 * 监听搜索条件变化，自动触发搜索（防抖可选）
 */
watch(
  () => [searchCriteria.time_range, searchCriteria.risk_level],
  () => {
    // 如果选择自定义时间范围，不自动触发搜索（等待用户选择完时间）
    if (searchCriteria.time_range === 'custom') {
      return
    }
    handleSearch()
  }
)

/**
 * 监听自定义时间变化，自动触发搜索
 */
watch(
  () => [customTimeStart.value, customTimeEnd.value],
  () => {
    // 只有当两个时间都有值时才触发搜索
    if (customTimeStart.value && customTimeEnd.value) {
      handleSearch()
    }
  }
)
</script>

<style scoped>
/* 确保输入框在深色主题下可见 */
:deep(.el-input__wrapper) {
  background-color: rgba(30, 30, 30, 0.8);
  box-shadow: 0 0 0 1px #374151 inset;
}

:deep(.el-input__inner) {
  color: #d1d5db;
}

:deep(.el-select .el-input__wrapper) {
  background-color: rgba(30, 30, 30, 0.8);
  box-shadow: 0 0 0 1px #374151 inset;
}

:deep(.el-select .el-input__inner) {
  color: #d1d5db;
}
</style>
