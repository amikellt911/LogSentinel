<template>
  <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
    <!-- 卡片 1：今日业务健康分 -->
    <div
      class="bg-gray-800/40 border border-gray-700 rounded-lg p-5 hover:border-gray-600 transition-all duration-300 group relative overflow-hidden"
    >
      <div class="flex items-center justify-between mb-3">
        <h3 class="text-xs uppercase tracking-widest text-gray-500 font-bold">
          {{ $t('serviceMonitor.healthScore.title') }}
        </h3>
        <el-icon class="w-5 h-5 transition-colors" :class="getHealthScoreColor(healthScore)">
          <CircleCheck />
        </el-icon>
      </div>

      <div class="flex items-center gap-4">
        <!-- 圆形进度条（简化版：使用 SVG） -->
        <div class="relative w-20 h-20">
          <svg class="w-full h-full transform -rotate-90">
            <!-- 背景圆环 -->
            <circle
              cx="40"
              cy="40"
              r="35"
              fill="none"
              stroke="#374151"
              stroke-width="6"
            />
            <!-- 进度圆环 -->
            <circle
              cx="40"
              cy="40"
              r="35"
              fill="none"
              :stroke="getHealthScoreColor(healthScore).replace('text-', '')"
              stroke-width="6"
              stroke-linecap="round"
              :stroke-dasharray="circumference"
              :stroke-dashoffset="circumference - (healthScore / 100) * circumference"
              class="transition-all duration-1000 ease-out"
            />
          </svg>
          <div class="absolute inset-0 flex items-center justify-center">
            <span
              class="text-2xl font-mono font-bold"
              :class="getHealthScoreColor(healthScore)"
            >
              {{ healthScore }}
            </span>
          </div>
        </div>

        <!-- 趋势指示 -->
        <div class="flex-1">
          <div class="text-xs text-gray-500 mb-1">
            {{ $t('serviceMonitor.healthScore.label') }}
          </div>
          <div
            class="text-sm font-bold flex items-center gap-1"
            :class="healthScore > 90 ? 'text-green-400' : (healthScore > 70 ? 'text-yellow-400' : 'text-red-400')"
          >
            <el-icon>
              <component :is="healthTrend > 0 ? ArrowUp : ArrowDown" />
            </el-icon>
            {{ healthTrend > 0 ? '+' : '' }}{{ healthTrend }}%
          </div>
        </div>
      </div>

      <!-- 背景装饰 -->
      <div
        class="absolute -bottom-4 -right-4 w-24 h-24 rounded-full opacity-0 group-hover:opacity-100 transition-opacity duration-300"
        :class="getHealthScoreBgColor(healthScore)"
      ></div>
    </div>

    <!-- 卡片 2：当前错误率 -->
    <div
      class="bg-gray-800/40 border border-gray-700 rounded-lg p-5 hover:border-gray-600 transition-all duration-300 group relative overflow-hidden"
    >
      <div class="flex items-center justify-between mb-3">
        <h3 class="text-xs uppercase tracking-widest text-gray-500 font-bold">
          {{ $t('serviceMonitor.errorRate.title') }}
        </h3>
        <el-icon class="w-5 h-5 text-red-500 group-hover:text-red-400 transition-colors">
          <Warning />
        </el-icon>
      </div>

      <div class="flex items-center gap-4">
        <!-- 错误率数字展示 -->
        <div class="flex-1">
          <div class="text-3xl font-mono font-bold text-red-400 flex items-baseline gap-1">
            {{ errorRate }}<span class="text-lg text-red-500">%</span>
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('serviceMonitor.errorRate.label') }}
          </div>
        </div>

        <!-- 趋势箭头 -->
        <div
          class="px-3 py-1.5 rounded-full text-xs font-bold flex items-center gap-1"
          :class="errorRateTrend > 0 ? 'bg-red-900/30 text-red-400' : 'bg-green-900/30 text-green-400'"
        >
          <el-icon>
            <component :is="errorRateTrend > 0 ? ArrowUp : ArrowDown" />
          </el-icon>
          {{ errorRateTrend > 0 ? '+' : '' }}{{ errorRateTrend }}%
        </div>
      </div>

      <!-- 背景装饰 -->
      <div class="absolute -bottom-4 -right-4 w-24 h-24 bg-red-500/10 rounded-full opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
    </div>

    <!-- 卡片 3：平均业务耗时 -->
    <div
      class="bg-gray-800/40 border border-gray-700 rounded-lg p-5 hover:border-gray-600 transition-all duration-300 group relative overflow-hidden"
    >
      <div class="flex items-center justify-between mb-3">
        <h3 class="text-xs uppercase tracking-widest text-gray-500 font-bold">
          {{ $t('serviceMonitor.avgDuration.title') }}
        </h3>
        <el-icon class="w-5 h-5 text-blue-500 group-hover:text-blue-400 transition-colors">
          <Timer />
        </el-icon>
      </div>

      <div class="flex items-center gap-4">
        <!-- 耗时数字展示 -->
        <div class="flex-1">
          <div class="text-3xl font-mono font-bold text-blue-400 flex items-baseline gap-1">
            {{ avgDuration }}<span class="text-lg text-blue-500">ms</span>
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('serviceMonitor.avgDuration.label') }}
          </div>
        </div>

        <!-- 对比上小时变化 -->
        <div
          class="px-3 py-1.5 rounded-full text-xs font-bold flex items-center gap-1"
          :class="avgDurationTrend > 0 ? 'bg-yellow-900/30 text-yellow-400' : 'bg-green-900/30 text-green-400'"
        >
          <el-icon>
            <component :is="avgDurationTrend > 0 ? ArrowUp : ArrowDown" />
          </el-icon>
          {{ avgDurationTrend > 0 ? '+' : '' }}{{ avgDurationTrend }}ms
        </div>
      </div>

      <!-- 背景装饰 -->
      <div class="absolute -bottom-4 -right-4 w-24 h-24 bg-blue-500/10 rounded-full opacity-0 group-hover:opacity-100 transition-opacity duration-300"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { CircleCheck, Warning, Timer, ArrowUp, ArrowDown } from '@element-plus/icons-vue'

// TODO: Replace with real API
// TODO: 替换为真实 API 数据
const healthScore = ref(98) // 业务健康分
const healthTrend = ref(2) // 对比上小时变化（百分比）

const errorRate = ref('2.5') // 错误率（百分比字符串）
const errorRateTrend = ref(0.3) // 对比上小时变化（百分比）

const avgDuration = ref(450) // 平均耗时（毫秒）
const avgDurationTrend = ref(-15) // 对比上小时变化（毫秒）

// SVG 圆环周长计算（2 * π * r）
const circumference = 2 * Math.PI * 35

/**
 * 根据健康分返回对应的颜色类名
 * @param score - 健康分数（0-100）
 * @returns Tailwind CSS 颜色类名
 */
function getHealthScoreColor(score: number): string {
  if (score > 90) return 'text-green-500'
  if (score > 70) return 'text-yellow-500'
  return 'text-red-500'
}

/**
 * 根据健康分返回对应的背景颜色类名
 * @param score - 健康分数（0-100）
 * @returns Tailwind CSS 背景颜色类名
 */
function getHealthScoreBgColor(score: number): string {
  if (score > 90) return 'bg-green-500/10'
  if (score > 70) return 'bg-yellow-500/10'
  return 'bg-red-500/10'
}
</script>

<style scoped>
/* 卡片悬停效果增强 */
.group:hover .group-hover\:opacity-100 {
  opacity: 1;
}
</style>
