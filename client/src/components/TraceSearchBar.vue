<template>
  <div class="bg-gray-800/30 p-4 rounded border border-gray-700">
    <div class="flex flex-wrap gap-4 items-end">
      <!-- Trace ID 精确匹配 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('traceExplorer.search.traceId') }}</label>
        <el-input
          v-model="searchCriteria.trace_id"
          :placeholder="$t('traceExplorer.search.traceIdPlaceholder')"
          size="small"
          clearable
          class="w-48"
        />
      </div>

      <!-- 服务名称下拉选择 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('traceExplorer.search.serviceName') }}</label>
        <el-select
          v-model="searchCriteria.service_name"
          :placeholder="$t('traceExplorer.search.allServices')"
          size="small"
          clearable
          class="w-40"
        >
          <el-option label="user-service" value="user-service" />
          <el-option label="order-service" value="order-service" />
          <el-option label="payment-service" value="payment-service" />
          <el-option label="inventory-service" value="inventory-service" />
          <el-option label="notification-service" value="notification-service" />
        </el-select>
      </div>

      <!-- 时间范围快捷选择 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('traceExplorer.search.timeRange') }}</label>
        <el-select
          v-model="searchCriteria.time_range"
          size="small"
          class="w-32"
          @change="handleTimeRangeChange"
        >
          <el-option label="1h" value="1h" />
          <el-option label="6h" value="6h" />
          <el-option label="24h" value="24h" />
          <el-option label="Custom" value="custom" />
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

      <!-- 风险等级多选 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('traceExplorer.search.riskLevel') }}</label>
        <el-select
          v-model="searchCriteria.risk_level"
          :placeholder="$t('traceExplorer.search.allLevels')"
          size="small"
          clearable
          multiple
          collapse-tags
          class="w-48"
        >
          <el-option label="Critical" value="Critical" />
          <el-option label="Error" value="Error" />
          <el-option label="Warning" value="Warning" />
          <el-option label="Info" value="Info" />
          <el-option label="Safe" value="Safe" />
        </el-select>
      </div>

      <!-- 最小耗时过滤 -->
      <div class="flex flex-col gap-1">
        <label class="text-gray-400 text-xs font-bold uppercase">{{ $t('traceExplorer.search.minDuration') }}</label>
        <el-input-number
          v-model="searchCriteria.min_duration"
          :min="0"
          :step="100"
          size="small"
          :placeholder="$t('traceExplorer.search.minDurationPlaceholder')"
          class="w-32"
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
import { ref, reactive, watch } from 'vue'
import { Search } from '@element-plus/icons-vue'
import type { TraceSearchCriteria } from '../types/trace'

// Props
interface Props {
  loading?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  loading: false
})

// Emits
const emit = defineEmits<{
  search: [criteria: TraceSearchCriteria]
  reset: []
}>()

// 搜索条件
const searchCriteria = reactive<TraceSearchCriteria>({
  trace_id: '',
  service_name: '',
  time_range: '24h',
  risk_level: [],
  min_duration: undefined
})

// 自定义时间范围
const customTimeStart = ref<string>('')
const customTimeEnd = ref<string>('')

/**
 * 处理搜索
 */
function handleSearch() {
  // 如果选择自定义时间范围，添加自定义时间
  const criteria = { ...searchCriteria }
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
  searchCriteria.service_name = ''
  searchCriteria.time_range = '24h'
  searchCriteria.risk_level = []
  searchCriteria.min_duration = undefined
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

// TODO: URL 参数同步（刷新保持搜索条件）
// TODO: 替换为真实 API 调用
</script>

<style scoped>
/* 确保时间选择器在深色主题下可见 */
:deep(.el-input__wrapper) {
  background-color: rgba(30, 30, 30, 0.8);
  box-shadow: 0 0 0 1px #374151 inset;
}

:deep(.el-input__inner) {
  color: #d1d5db;
}
</style>
