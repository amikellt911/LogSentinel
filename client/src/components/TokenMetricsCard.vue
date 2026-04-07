<template>
  <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
    <!-- 卡片 1：总 Token -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.totalTokens') }}
          </div>
          <div class="text-2xl font-bold text-blue-400 font-mono">
            {{ formatNumber(systemStore.totalTokens) }}
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('aiEngine.tokens') }}
          </div>
        </div>
        <div class="w-12 h-12 rounded-lg bg-blue-900/20 border border-blue-500/30 flex items-center justify-center">
          <el-icon :size="24" class="text-blue-400">
            <DataLine />
          </el-icon>
        </div>
      </div>
    </div>

    <!-- 卡片 2：输入 Token -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.input') }}
          </div>
          <div class="text-2xl font-bold text-green-400 font-mono">
            {{ formatNumber(systemStore.inputTokens) }}
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('aiEngine.tokens') }}
          </div>
        </div>
        <div class="w-12 h-12 rounded-lg bg-green-900/20 border border-green-500/30 flex items-center justify-center">
          <el-icon :size="24" class="text-green-400">
            <TrendCharts />
          </el-icon>
        </div>
      </div>
    </div>

    <!-- 卡片 3：输出 Token -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.output') }}
          </div>
          <div class="text-2xl font-bold text-yellow-400 font-mono">
            {{ formatNumber(systemStore.outputTokens) }}
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('aiEngine.tokens') }}
          </div>
        </div>
        <div class="w-12 h-12 rounded-lg bg-yellow-900/20 border border-yellow-500/30 flex items-center justify-center">
          <el-icon :size="24" class="text-yellow-400">
            <Coin />
          </el-icon>
        </div>
      </div>
    </div>

    <!-- 卡片 4：平均每次 AI 调用 Token 数 -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.avgTokensPerCall') }}
          </div>
          <div class="text-2xl font-bold text-purple-400 font-mono">
            {{ formatNumber(systemStore.avgTokensPerCall) }}
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('dashboard.calls') }}
          </div>
        </div>
        <div class="w-12 h-12 rounded-lg bg-purple-900/20 border border-purple-500/30 flex items-center justify-center">
          <el-icon :size="24" class="text-purple-400">
            <Document />
          </el-icon>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useSystemStore } from '../stores/system'
import { DataLine, TrendCharts, Coin, Document } from '@element-plus/icons-vue'

/**
 * Token 卡片这一步先收口成“纯 token 真值展示”：
 * 只保留总量、输入、输出和平均每次 AI 调用 token，
 * 不再继续混节省比例、预估成本这种容易误导演示口径的字段。
 */
// Token 卡片现在直接读取系统监控 store 的真值，不再在组件内部留一套本地 mock。
// 这样 token 的 provider 真值/估算回退逻辑只在后端和 store 里维护一次，展示层不再重复发明状态。
const systemStore = useSystemStore()

/**
 * 格式化数字（添加千分位分隔符）
 */
function formatNumber(num: number): string {
  return num.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',')
}
</script>

<style scoped>
/* 无额外样式，使用 Tailwind CSS */
</style>
