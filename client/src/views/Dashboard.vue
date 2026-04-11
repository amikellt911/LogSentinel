<template>
  <div class="h-full flex flex-col p-6 space-y-6 overflow-y-auto custom-scrollbar">

    <!-- 顶部：这里只保留系统运行态卡片，不再混业务风险和演示味很重的副文案。 -->
    <!-- 这一刀已经改成直接读取后端系统运行态快照，不再继续消费旧 dashboard mock 字段。 -->
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
      <MetricCard
        :title="$t('dashboard.totalLogs')"
        :value="formatNumber(systemStore.totalLogsProcessed)"
        :icon="Files"
        status-color="text-success"
      />
      <MetricCard
        :title="$t('dashboard.netLatency')"
        :value="formatNumber(systemStore.aiQueueWaitMs)"
        unit="ms"
        :icon="Lightning"
        status-color="text-success"
      />
      <MetricCard
        :title="$t('dashboard.aiLatency')"
        :value="formatNumber(systemStore.aiInferenceLatencyMs)"
        unit="ms"
        :icon="Timer"
        status-color="text-warning"
      />

      <MetricCard
        :title="$t('dashboard.aiTrigger')"
        :value="formatNumber(systemStore.aiCallTotal)"
        :icon="Cpu"
        status-color="text-primary"
        :unit="$t('dashboard.calls')"
      />
      <MetricCard
        :title="$t('dashboard.memory')"
        :value="formatNumber(systemStore.memoryRssMb)"
        unit="MB"
        :icon="Histogram"
        status-color="text-primary"
      />
      <MetricCard
        :title="$t('dashboard.backpressure')"
        :value="$t(`dashboard.${systemStore.backpressureStatus.toLowerCase()}`)"
        :icon="Warning"
        :status-color="backpressureColor"
      />
    </div>

    <!-- 中部：Token 卡片只展示真实可累计的 token 指标，先不再混成本和节省比例。 -->
    <TokenMetricsCard />

    <!-- 底部：系统吞吐图只保留入口和出口两条线，避免混进第三种语义把图看花。 -->
    <div class="min-h-[350px] bg-gray-800/30 border border-gray-700 rounded p-4 relative flex flex-col">
      <div class="flex justify-between items-center mb-4">
        <h3 class="text-sm font-bold text-gray-400 uppercase tracking-widest flex items-center gap-2">
          <span class="w-2 h-2 rounded-full bg-green-500 animate-pulse"></span>
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

// 图表实例只负责系统吞吐趋势，不再承担业务风险之类的混合展示。
const chartRef = ref<HTMLElement | null>(null)
let myChart: echarts.ECharts | null = null

const backpressureColor = computed(() => {
   switch(systemStore.backpressureStatus) {
      case 'Normal': return 'text-success'
      case 'Active': return 'text-warning'
      case 'Full': return 'text-danger'
      default: return 'text-gray-500'
   }
})
// 背压状态现在只显示综合结论，不再附带单一 queue 百分比。
// 因为后端背压已经是多因素联合判断，再把某一个队列占用塞成副文案，反而会把语义讲脏。

// 内存卡这一步直接显示“当前进程 RSS MB”，不再用假的百分比进度条。
// 这样用户看到的是 LogSentinel 进程自己吃了多少内存，而不是来源不清的整机占用率。

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

watch(locale, () => {
   // 语言切换后重建图表实例，避免图例和 tooltip 仍然停留在旧文案。
   nextTick(() => {
      myChart?.dispose()
      initCharts()
      updateMainChart()
   })
})

watch(() => systemStore.chartData, () => {
  updateMainChart()
}, { deep: true })

const resizeHandler = () => {
   myChart?.resize()
}

onMounted(() => {
  // 这页现在定位成“系统监控”，所以首屏只需要初始化吞吐图和当前快照数据，不做额外动画逻辑。
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
