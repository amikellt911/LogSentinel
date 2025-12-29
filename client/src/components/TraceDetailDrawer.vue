<template>
  <el-drawer
    v-model="visible"
    :title="`${$t('traceExplorer.drawer.title')}: ${traceDetail?.trace_id || ''}`"
    direction="rtl"
    :size="drawerSize"
    @close="handleClose"
  >
    <template #header>
      <div class="flex items-center gap-3">
        <span class="text-lg font-bold text-gray-200">{{ $t('traceExplorer.drawer.title') }}</span>
        <span class="font-mono text-sm text-blue-400">{{ traceDetail?.trace_id }}</span>
      </div>
    </template>

    <div v-if="traceDetail" class="h-full flex flex-col space-y-6 overflow-y-auto custom-scrollbar">
      <!-- 顶部：AI 根因分析 -->
      <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4">
        <div class="flex items-center justify-between mb-3">
          <h3 class="text-sm font-bold text-purple-400 uppercase tracking-wider flex items-center gap-2">
            <el-icon><MagicStick /></el-icon>
            {{ $t('traceExplorer.drawer.aiAnalysis') }}
          </h3>
          <!-- 风险等级徽章 -->
          <span
            v-if="traceDetail.ai_analysis"
            class="px-3 py-1 rounded-full text-xs font-bold"
            :class="getLevelBadgeClass(traceDetail.ai_analysis.risk_level)"
          >
            {{ traceDetail.ai_analysis.risk_level }}
          </span>
        </div>

        <div v-if="traceDetail.ai_analysis" class="space-y-3">
          <!-- 根因总结 -->
          <div>
            <div class="text-xs text-gray-500 uppercase mb-1">{{ $t('traceExplorer.drawer.summary') }}</div>
            <p class="text-gray-300 text-sm leading-relaxed">{{ traceDetail.ai_analysis.summary }}</p>
          </div>

          <!-- 根因分析 -->
          <div>
            <div class="text-xs text-gray-500 uppercase mb-1">{{ $t('traceExplorer.drawer.rootCause') }}</div>
            <p class="text-gray-300 text-sm leading-relaxed">{{ traceDetail.ai_analysis.root_cause }}</p>
          </div>

          <!-- 解决建议 -->
          <div>
            <div class="text-xs text-gray-500 uppercase mb-1">{{ $t('traceExplorer.drawer.solution') }}</div>
            <p class="text-gray-300 text-sm leading-relaxed">{{ traceDetail.ai_analysis.solution }}</p>
          </div>
        </div>

        <div v-else class="text-gray-500 text-sm italic">
          {{ $t('traceExplorer.drawer.noAnalysis') }}
        </div>
      </div>

      <!-- 中部：瀑布图（调用链可视化） -->
      <div class="flex-1">
        <TraceWaterfall :spans="traceDetail.spans" />
      </div>

      <!-- 底部：Span 详细列表（可折叠） -->
      <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4">
        <div class="flex items-center justify-between mb-3 cursor-pointer" @click="toggleSpanList">
          <h3 class="text-sm font-bold text-gray-400 uppercase tracking-wider flex items-center gap-2">
            <el-icon><List /></el-icon>
            {{ $t('traceExplorer.drawer.spanList') }} ({{ traceDetail.spans.length }})
          </h3>
          <el-icon :class="{ 'rotate-180': spanListExpanded }">
            <ArrowDown />
          </el-icon>
        </div>

        <div v-show="spanListExpanded" class="space-y-2">
          <div
            v-for="span in traceDetail.spans"
            :key="span.span_id"
            class="bg-gray-900/50 rounded p-3 border border-gray-700 hover:border-gray-600 transition-colors"
          >
            <div class="flex justify-between items-start mb-2">
              <div class="flex-1">
                <div class="flex items-center gap-2 mb-1">
                  <span class="text-sm font-bold text-gray-200">{{ span.service_name }}</span>
                  <span
                    class="px-2 py-0.5 rounded text-xs font-bold"
                    :class="getSpanStatusClass(span.status)"
                  >
                    {{ span.status }}
                  </span>
                </div>
                <div v-if="span.operation" class="text-xs text-gray-500 font-mono">
                  {{ span.operation }}
                </div>
              </div>
              <div class="text-right">
                <div class="text-xs text-gray-500">Duration</div>
                <div class="font-mono text-sm" :class="getDurationClass(span.duration)">
                  {{ span.duration }}ms
                </div>
              </div>
            </div>

            <div class="flex gap-4 text-xs text-gray-500 font-mono">
              <div>Span ID: {{ span.span_id }}</div>
              <div>Start: {{ span.start_time }}ms</div>
              <div v-if="span.parent_id">Parent: {{ span.parent_id }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { MagicStick, List, ArrowDown } from '@element-plus/icons-vue'
import TraceWaterfall from './TraceWaterfall.vue'
import type { TraceDetail } from '../types/trace'

// Props
interface Props {
  modelValue: boolean
  traceDetail: TraceDetail | null
}

const props = defineProps<Props>()

// Emits
const emit = defineEmits<{
  'update:modelValue': [value: boolean]
}>()

// 响应式状态
const visible = computed({
  get: () => props.modelValue,
  set: (value) => emit('update:modelValue', value)
})

const spanListExpanded = ref(true)

/**
 * 抽屉宽度（响应式）
 */
const drawerSize = computed(() => {
  // 桌面端 70%，平板 80%，移动端 100%
  if (window.innerWidth >= 1024) return '70%'
  if (window.innerWidth >= 768) return '80%'
  return '100%'
})

/**
 * 根据风险等级返回徽章样式类名
 */
function getLevelBadgeClass(level: string): string {
  switch (level) {
    case 'Critical':
      return 'bg-red-900/50 text-red-400 border border-red-500/30'
    case 'Error':
      return 'bg-orange-900/50 text-orange-400 border border-orange-500/30'
    case 'Warning':
      return 'bg-yellow-900/50 text-yellow-400 border border-yellow-500/30'
    case 'Info':
      return 'bg-blue-900/50 text-blue-400 border border-blue-500/30'
    case 'Safe':
      return 'bg-green-900/50 text-green-400 border border-green-500/30'
    default:
      return 'bg-gray-700 text-gray-300'
  }
}

/**
 * 根据 Span 状态返回样式类名
 */
function getSpanStatusClass(status: string): string {
  switch (status) {
    case 'success':
      return 'bg-green-900/50 text-green-400 border border-green-500/30'
    case 'error':
      return 'bg-red-900/50 text-red-400 border border-red-500/30'
    case 'warning':
      return 'bg-yellow-900/50 text-yellow-400 border border-yellow-500/30'
    default:
      return 'bg-gray-700 text-gray-300'
  }
}

/**
 * 根据耗时返回样式类名
 */
function getDurationClass(duration: number): string {
  if (duration > 1000) return 'text-red-400 font-bold'
  if (duration > 500) return 'text-yellow-400 font-bold'
  return 'text-gray-300'
}

/**
 * 切换 Span 列表展开/折叠
 */
function toggleSpanList() {
  spanListExpanded.value = !spanListExpanded.value
}

/**
 * 关闭抽屉（清空数据）
 */
function handleClose() {
  visible.value = false
  spanListExpanded.value = true
  // TODO: 清空数据（释放内存）
}

/**
 * 监听抽屉打开/关闭，重置状态
 */
watch(visible, (newVal) => {
  if (!newVal) {
    spanListExpanded.value = true
  }
})
</script>

<style scoped>
/* 自定义滚动条样式 */
.custom-scrollbar::-webkit-scrollbar {
  width: 6px;
}

.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #374151;
  border-radius: 3px;
}

/* 抽屉样式覆盖 */
:deep(.el-drawer) {
  background-color: #1e1e1e;
}

:deep(.el-drawer__header) {
  padding: 20px;
  border-bottom: 1px solid #374151;
  margin-bottom: 0;
}

/* 旋转动画 */
.rotate-180 {
  transform: rotate(180deg);
  transition: transform 0.3s ease;
}
</style>
