<template>
  <div v-if="traceDetail" class="h-full flex flex-col space-y-6 overflow-y-auto custom-scrollbar">
    <!-- 瀑布图（调用链可视化） -->
    <div class="flex-1">
      <TraceWaterfall :spans="traceDetail.spans" />
    </div>

    <!-- Span 详细列表（可折叠） -->
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

          <!-- attributes 展示的是业务侧随 Span 上报的埋点事实，不是 AI 推理结论。
               答辩时可以直接对照这些字段说明模型为什么能判断金额和规则口径异常。 -->
          <div
            v-if="getAttributeEntries(span.attributes).length > 0"
            class="mt-3 rounded border border-gray-700/80 bg-black/20"
          >
            <div class="border-b border-gray-700/80 px-3 py-2 text-[11px] font-bold uppercase tracking-wider text-gray-500">
              Attributes
            </div>
            <div class="divide-y divide-gray-800/80">
              <div
                v-for="[key, value] in getAttributeEntries(span.attributes)"
                :key="key"
                class="grid grid-cols-[minmax(140px,220px)_1fr] gap-3 px-3 py-2 text-xs"
              >
                <div class="font-mono text-cyan-300 break-all">{{ key }}</div>
                <pre class="m-0 whitespace-pre-wrap break-all font-mono text-gray-300">{{ formatAttributeValue(value) }}</pre>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { List, ArrowDown } from '@element-plus/icons-vue'
import TraceWaterfall from './TraceWaterfall.vue'
import type { TraceDetail, TraceSpan } from '../types/trace'

// Props
interface Props {
  traceDetail: TraceDetail
}

defineProps<Props>()

// 响应式状态
const spanListExpanded = ref(true)

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

function getAttributeEntries(attributes: TraceSpan['attributes']) {
  return Object.entries(attributes ?? {})
}

function formatAttributeValue(value: unknown): string {
  if (typeof value === 'string') {
    const trimmed = value.trim()
    if ((trimmed.startsWith('{') && trimmed.endsWith('}')) ||
        (trimmed.startsWith('[') && trimmed.endsWith(']'))) {
      try {
        return JSON.stringify(JSON.parse(trimmed), null, 2)
      } catch {
        return value
      }
    }
    return value
  }
  if (typeof value === 'number' || typeof value === 'boolean') {
    return String(value)
  }
  if (value === null) {
    return 'null'
  }
  return JSON.stringify(value, null, 2)
}

/**
 * 切换 Span 列表展开/折叠
 */
function toggleSpanList() {
  spanListExpanded.value = !spanListExpanded.value
}
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

/* 旋转动画 */
.rotate-180 {
  transform: rotate(180deg);
  transition: transform 0.3s ease;
}
</style>
