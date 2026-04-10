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
          <span class="px-3 py-1 rounded-full text-xs font-bold border" :class="getAiStatusBadgeClass(traceDetail.ai_status)">
            {{ getAiStatusLabel(traceDetail.ai_status) }}
          </span>
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

      <div v-else class="space-y-2">
        <!-- 没有 ai_analysis 不等于“什么都不知道”。
             这里优先展示后端落下来的 ai_status，明确告诉用户是手动关闭、熔断跳过还是调用失败。 -->
        <div class="text-gray-300 text-sm">
          {{ getAiStatusDescription(traceDetail.ai_status) }}
        </div>
        <div v-if="traceDetail.ai_error" class="rounded border border-red-500/20 bg-red-950/20 px-3 py-2 text-xs text-red-200 whitespace-pre-wrap break-all">
          {{ traceDetail.ai_error }}
        </div>
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

function getAiStatusLabel(status: string): string {
  switch (status) {
    case 'completed':
      return '已完成'
    case 'skipped_manual':
      return '已关闭'
    case 'skipped_circuit':
      return '熔断跳过'
    case 'failed_primary':
      return '主路失败'
    case 'failed_both':
      return '双路失败'
    default:
      return '处理中'
  }
}

function getAiStatusDescription(status: string): string {
  switch (status) {
    case 'skipped_manual':
      return 'AI 分析已被手动关闭，本次 trace 只保留聚合结果。'
    case 'skipped_circuit':
      return 'AI 当前处于熔断跳过状态，本次 trace 未发起分析。'
    case 'failed_primary':
      return '主 AI 分析失败，本次 trace 没有生成分析结果。'
    case 'failed_both':
      return '主 AI 和降级 AI 都失败了，本次 trace 没有生成分析结果。'
    default:
      return 'AI 分析尚未完成。'
  }
}

function getAiStatusBadgeClass(status: string): string {
  switch (status) {
    case 'completed':
      return 'bg-emerald-900/40 text-emerald-300 border-emerald-500/30'
    case 'skipped_manual':
      return 'bg-slate-700/50 text-slate-200 border-slate-500/30'
    case 'skipped_circuit':
      return 'bg-amber-900/40 text-amber-300 border-amber-500/30'
    case 'failed_primary':
    case 'failed_both':
      return 'bg-rose-900/40 text-rose-300 border-rose-500/30'
    default:
      return 'bg-blue-900/40 text-blue-300 border-blue-500/30'
  }
}
</script>

<style scoped>
/* AI 分析卡片样式 */
</style>
