<template>
  <div v-if="traceDetail" class="h-full flex flex-col">
    <!-- AI 根因分析卡片 -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4">
      <div class="flex items-center justify-between mb-3">
        <h3 class="text-sm font-bold text-purple-400 uppercase tracking-wider flex items-center gap-2">
          <el-icon><MagicStick /></el-icon>
          {{ $t('traceExplorer.drawer.aiAnalysis') }}
        </h3>
        <div class="flex items-center gap-2">
          <!-- 标签 -->
          <div v-if="traceDetail.tags && traceDetail.tags.length > 0" class="flex gap-1">
            <span
              v-for="tag in traceDetail.tags"
              :key="tag"
              class="text-[10px] px-2 py-0.5 rounded-full border border-gray-600 text-gray-400 bg-gray-900/50"
            >
              #{{ tag }}
            </span>
          </div>
          <!-- 风险等级徽章 -->
          <span
            v-if="traceDetail.ai_analysis"
            class="px-3 py-1 rounded-full text-xs font-bold"
            :class="getLevelBadgeClass(traceDetail.ai_analysis.risk_level)"
          >
            {{ traceDetail.ai_analysis.risk_level }}
          </span>
        </div>
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
  </div>
</template>

<script setup lang="ts">
import { MagicStick } from '@element-plus/icons-vue'
import type { TraceDetail } from '../types/trace'

// Props
interface Props {
  traceDetail: TraceDetail
}

defineProps<Props>()

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
</script>

<style scoped>
/* AI 分析卡片样式 */
</style>
