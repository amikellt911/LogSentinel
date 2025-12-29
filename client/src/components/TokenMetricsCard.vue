<template>
  <div class="bg-gray-800 rounded-lg p-4 border border-gray-700 shadow-lg relative overflow-hidden group hover:border-gray-600 transition-colors">
    <!-- 图标 -->
    <div class="absolute top-3 right-3 text-gray-700 group-hover:text-gray-600 transition-colors">
      <el-icon :size="48"><component :is="icon" /></el-icon>
    </div>

    <!-- 内容 -->
    <div class="relative z-10">
      <div class="text-xs text-gray-500 uppercase font-bold tracking-wider mb-1">{{ title }}</div>
      <div class="text-2xl font-mono font-bold text-gray-200 flex items-end gap-2">
        <span :class="valueClass">{{ value }}</span>
        <span v-if="unit" class="text-sm text-gray-500 mb-1 font-normal">{{ unit }}</span>
      </div>

      <!-- 辅助文本 / 趋势 -->
      <div v-if="subtext" class="mt-2 text-xs flex items-center gap-1" :class="subtextClass">
        <el-icon v-if="trend === 'up'"><Top /></el-icon>
        <el-icon v-if="trend === 'down'"><Bottom /></el-icon>
        {{ subtext }}
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Top, Bottom } from '@element-plus/icons-vue'

const props = defineProps<{
  title: string
  value: string | number
  unit?: string
  icon: any
  trend?: 'up' | 'down' | 'neutral'
  subtext?: string
  status?: 'success' | 'warning' | 'danger' | 'info'
}>()

// 根据状态计算数值颜色
const valueClass = computed(() => {
  switch (props.status) {
    case 'success': return 'text-green-400'
    case 'warning': return 'text-yellow-400'
    case 'danger': return 'text-red-400'
    default: return 'text-gray-200'
  }
})

// 根据趋势计算辅助文本颜色
const subtextClass = computed(() => {
  if (props.trend === 'up') return 'text-green-500'
  if (props.trend === 'down') return 'text-red-500'
  // 如果没有明确趋势，默认使用灰色
  return 'text-gray-500'
})
</script>
