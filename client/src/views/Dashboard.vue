<template>
  <div class="h-full flex flex-col p-6 space-y-6 overflow-y-auto custom-scrollbar">

    <!-- 顶部：6 个性能指标卡片 -->
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
      <!-- Row 1: Performance -->
      <MetricCard
        :title="$t('dashboard.totalLogs')"
        :value="formatNumber(systemStore.totalLogsProcessed)"
        :icon="Files"
        status-color="text-success"
        subValue="+12.5% vs last hour"
      />
      <MetricCard
        :title="$t('dashboard.netLatency')"
        :value="systemStore.netLatency"
        unit="ms"
        :icon="Lightning"
        status-color="text-success"
        subValue="⚡ Fast"
      />
      <MetricCard
        :title="$t('dashboard.aiLatency')"
        :value="systemStore.aiLatency"
        unit="ms"
        :icon="Timer"
        status-color="text-warning"
        subValue="~0.8s avg"
      />

      <!-- Row 2: Health -->
      <MetricCard
        :title="$t('dashboard.aiTrigger')"
        :value="formatNumber(systemStore.aiTriggerCount)"
        :icon="Cpu"
        status-color="text-primary"
        :unit="$t('dashboard.calls')"
      />
      <MetricCard
        :title="$t('dashboard.memory')"
        :value="systemStore.memoryPercent + '%'"
        :icon="Histogram"
        :status-color="systemStore.memoryPercent > 80 ? 'text-danger' : 'text-success'"
      >
         <div class="w-full bg-gray-700 h-1.5 mt-3 rounded-full overflow-hidden">
            <div
              class="h-full transition-all duration-500"
              :class="systemStore.memoryPercent > 80 ? 'bg-red-500' : 'bg-green-500'"
              :style="{ width: systemStore.memoryPercent + '%' }"
            ></div>
         </div>
      </MetricCard>
      <MetricCard
        :title="$t('dashboard.backpressure')"
        :value="$t(`dashboard.${systemStore.backpressureStatus.toLowerCase()}`)"
        :icon="Warning"
        :status-color="backpressureColor"
        :subValue="$t('dashboard.queue') + ': ' + systemStore.queuePercent + '%'"
      />
    </div>

    <!-- 中部：4 个 Token 指标卡片 -->
    <TokenMetricsCard />

    <!-- 底部：实时吞吐量图表 -->
    <div class="min-h-[350px] bg-gray-800/30 border border-gray-700 rounded p-4 relative flex flex-col">
      <div class="flex justify-between items-center mb-4">
        <h3 class="text-sm font-bold text-gray-400 uppercase tracking-widest flex items-center gap-2">
          <span class="w-2 h-2 rounded-full" :class="systemStore.isRunning ? 'bg-green-500 animate-pulse' : 'bg-gray-600'"></span>
          {{ $t('dashboard.realtimeTitle') }}
        </h3>
        <div class="flex gap-4 text-xs font-mono">
          <span class="flex items-center gap-2"><span class="w-3 h-1 bg-blue-500"></span> {{ $t('dashboard.ingestRate') }}</span>
          <span class="flex items-center gap-2"><span class="w-3 h-1 bg-purple-500"></span> {{ $t('dashboard.aiRate') }}</span>
        </div>
      </div>
      <div ref="chartRef" class="flex-1 w-full h-full min-h-[300px]"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, computed, nextTick } from 'vue'
import { useSystemStore } from '../stores/system'
import MetricCard from '../components/MetricCard.vue'
import TokenMetricsCard from '../components/TokenMetricsCard.vue'
import { Files, Cpu, Warning, Histogram, Lightning, Timer } from '@element-plus/icons-vue'
import * as echarts from 'echarts'
import { useI18n } from 'vue-i18n'

const { t, locale } = useI18n()
const systemStore = useSystemStore()

// Refs
const chartRef = ref<HTMLElement | null>(null)
let myChart: echarts.ECharts | null = null

// Computeds
const backpressureColor = computed(() => {
   switch(systemStore.backpressureStatus) {
      case 'Normal': return 'text-success'
      case 'Active': return 'text-warning'
      case 'Full': return 'text-danger'
      default: return 'text-gray-500'
   }
})

function formatNumber(num: number) {
  return new Intl.NumberFormat('en-US').format(num)
}

// --- Charts ---

function initCharts() {
  if (chartRef.value) {
    // @ts-ignore
    myChart = echarts.init(chartRef.value, 'dark', { renderer: 'canvas', backgroundColor: 'transparent' })
    const option = {
        grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true, borderColor: '#333' },
        tooltip: { trigger: 'axis', axisPointer: { type: 'cross', label: { backgroundColor: '#6a7985' } } },
        xAxis: { type: 'category', boundaryGap: false, data: [], axisLine: { lineStyle: { color: '#555' } }, axisLabel: { color: '#888', fontFamily: 'monospace' } },
        yAxis: { type: 'value', splitLine: { lineStyle: { color: '#333', type: 'dashed' } }, axisLabel: { color: '#888', fontFamily: 'monospace' } },
        series: [
        { name: t('dashboard.ingestRate'), type: 'line', smooth: true, showSymbol: false, lineStyle: { width: 2, color: '#3b82f6' }, areaStyle: { color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [{ offset: 0, color: 'rgba(59, 130, 246, 0.3)' }, { offset: 1, color: 'rgba(59, 130, 246, 0.05)' }]) }, data: [] },
        { name: t('dashboard.aiRate'), type: 'line', smooth: true, showSymbol: false, lineStyle: { width: 2, color: '#a855f7' }, areaStyle: { color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [{ offset: 0, color: 'rgba(168, 85, 247, 0.3)' }, { offset: 1, color: 'rgba(168, 85, 247, 0.05)' }]) }, data: [] }
        ],
        animationEasing: 'linear',
        animationDurationUpdate: 500
    }
    myChart.setOption(option as any)
  }
}

function updateMainChart() {
  if (!myChart) return
  const times = systemStore.chartData.map(d => d.time)
  const qps = systemStore.chartData.map(d => d.qps)
  const ai = systemStore.chartData.map(d => d.aiRate)

  myChart.setOption({
    xAxis: { data: times },
    series: [ { data: qps }, { data: ai } ]
  })
}

// Watchers
watch(locale, () => {
   // Re-render charts for language update
   nextTick(() => {
      myChart?.dispose()
      initCharts()
      updateMainChart()
   })
})

watch(() => systemStore.chartData, () => {
  updateMainChart()
}, { deep: true })

// Lifecycle
const resizeHandler = () => {
   myChart?.resize()
}

onMounted(() => {
  initCharts()
  window.addEventListener('resize', resizeHandler)
  updateMainChart() // Initial draw
})

onUnmounted(() => {
  window.removeEventListener('resize', resizeHandler)
  myChart?.dispose()
})
</script>

<style scoped>
.custom-scrollbar::-webkit-scrollbar {
  width: 6px;
}
.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #374151;
  border-radius: 3px;
}
</style>
