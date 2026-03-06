<template>
  <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
    <!-- 卡片 1：今日 Token 消耗总量 -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.todayTotalTokens') }}
          </div>
          <div class="text-2xl font-bold text-blue-400 font-mono">
            {{ formatNumber(metrics.todayTotalTokens) }}
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

    <!-- 卡片 2：节省比例（绿色突出） -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.savingRatio') }}
          </div>
          <div class="text-3xl font-bold text-green-400 font-mono">
            {{ metrics.savingRatio }}%
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('aiEngine.comparedToRaw') }}
          </div>
        </div>
        <div class="w-12 h-12 rounded-lg bg-green-900/20 border border-green-500/30 flex items-center justify-center">
          <el-icon :size="24" class="text-green-400">
            <TrendCharts />
          </el-icon>
        </div>
      </div>
    </div>

    <!-- 卡片 3：预估成本 -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.estimatedCost') }}
          </div>
          <div class="text-2xl font-bold text-yellow-400 font-mono">
            ${{ metrics.estimatedCost }}
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('aiEngine.today') }}
          </div>
        </div>
        <div class="w-12 h-12 rounded-lg bg-yellow-900/20 border border-yellow-500/30 flex items-center justify-center">
          <el-icon :size="24" class="text-yellow-400">
            <Coin />
          </el-icon>
        </div>
      </div>
    </div>

    <!-- 卡片 4：平均每批次 Token 数 -->
    <div class="bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg">
      <div class="flex items-center justify-between">
        <div>
          <div class="text-xs text-gray-500 uppercase font-semibold mb-1">
            {{ $t('aiEngine.avgTokensPerBatch') }}
          </div>
          <div class="text-2xl font-bold text-purple-400 font-mono">
            {{ formatNumber(metrics.avgTokensPerBatch) }}
          </div>
          <div class="text-xs text-gray-500 mt-1">
            {{ $t('aiEngine.perBatch') }}
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
import { reactive } from 'vue'
import { DataLine, TrendCharts, Coin, Document } from '@element-plus/icons-vue'

// TODO: Replace with real API
/**
 * Token 指标数据结构
 */
interface TokenMetrics {
  todayTotalTokens: number      // 今日 Token 消耗总量
  savingRatio: number            // 节省比例（百分比）
  estimatedCost: string          // 预估成本（美元）
  avgTokensPerBatch: number      // 平均每批次 Token 数
}

// Mock 数据
const metrics = reactive<TokenMetrics>({
  todayTotalTokens: 245760,      // 24.5万 tokens
  savingRatio: 85,               // 85%
  estimatedCost: '0.02',         // $0.02
  avgTokensPerBatch: 512         // 平均 512 tokens/batch
})

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
