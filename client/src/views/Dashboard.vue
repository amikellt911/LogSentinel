<template>
  <div class="h-full flex flex-col p-6 space-y-6 overflow-y-auto custom-scrollbar">
    
    <!-- Top Metrics Grid (2 Rows x 3 Cols) -->
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

    <!-- Main Chart -->
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

    <!-- Security Analysis Section -->
    <div class="flex flex-col lg:flex-row gap-4 min-h-[300px]">
       <!-- Left: Risk Distribution -->
       <div class="lg:w-[30%] bg-gray-800/30 border border-gray-700 rounded p-4 flex flex-col">
          <h3 class="text-xs font-bold text-gray-400 uppercase tracking-widest mb-2">{{ $t('dashboard.riskTitle') }}</h3>
          <div ref="riskChartRef" class="flex-1 w-full min-h-[200px]"></div>
       </div>

       <!-- Right: Recent Alerts -->
       <div class="lg:w-[70%] bg-gray-800/30 border border-gray-700 rounded p-4 flex flex-col">
          <h3 class="text-xs font-bold text-gray-400 uppercase tracking-widest mb-4">{{ $t('dashboard.recentAlerts') }}</h3>
          <div class="flex-1 overflow-hidden">
             <el-table 
               :data="systemStore.recentAlerts" 
               style="width: 100%; --el-table-bg-color: transparent; --el-table-tr-bg-color: transparent; --el-table-header-bg-color: transparent; --el-table-border-color: #374151; --el-table-text-color: #9ca3af; --el-table-header-text-color: #d1d5db;"
               size="small"
               :row-style="{ background: 'transparent' }"
               :header-row-style="{ background: 'transparent' }"
             >
                <el-table-column prop="time" :label="$t('dashboard.table.time')" width="100" />
                <el-table-column prop="service" :label="$t('dashboard.table.service')" width="140" />
                <el-table-column prop="level" :label="$t('dashboard.table.level')" width="100">
                   <template #default="{ row }">
                      <span class="px-2 py-0.5 rounded text-xs font-bold" :class="getLevelClass(row.level)">
                         {{ row.level }}
                      </span>
                   </template>
                </el-table-column>
                <el-table-column prop="summary" :label="$t('dashboard.table.summary')" />
             </el-table>
          </div>
       </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, computed, nextTick } from 'vue'
import { useSystemStore } from '../stores/system'
import MetricCard from '../components/MetricCard.vue'
import { Files, Cpu, Warning, Histogram, Lightning, Timer } from '@element-plus/icons-vue'
import * as echarts from 'echarts'
import { useI18n } from 'vue-i18n'

const { t, locale } = useI18n()
const systemStore = useSystemStore()

// Refs
const chartRef = ref<HTMLElement | null>(null)
const riskChartRef = ref<HTMLElement | null>(null)
let myChart: echarts.ECharts | null = null
let riskChart: echarts.ECharts | null = null

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

function getLevelClass(level: string) {
   switch(level) {
      case 'Critical': return 'bg-red-900/50 text-red-400'
      case 'Error': return 'bg-orange-900/50 text-orange-400'
      case 'Warning': return 'bg-yellow-900/50 text-yellow-400'
      case 'Info': return 'bg-blue-900/50 text-blue-400'
      case 'Safe': return 'bg-green-900/50 text-green-400'
      default: return 'bg-gray-700 text-gray-300'
   }
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

  if (riskChartRef.value) {
     // @ts-ignore
     riskChart = echarts.init(riskChartRef.value, 'dark', { renderer: 'canvas', backgroundColor: 'transparent' })
     updateRiskChart()
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

function updateRiskChart() {
   if (!riskChart) return
   
   const data = [
      { value: systemStore.riskStats.Critical, name: 'Critical', itemStyle: { color: '#F56C6C' } },
      { value: systemStore.riskStats.Error, name: 'Error', itemStyle: { color: '#E6A23C' } },
      { value: systemStore.riskStats.Warning, name: 'Warning', itemStyle: { color: '#FAC858' } },
      { value: systemStore.riskStats.Info, name: 'Info', itemStyle: { color: '#409EFF' } },
      { value: systemStore.riskStats.Safe, name: 'Safe', itemStyle: { color: '#67C23A' } }
   ]

   const option = {
      backgroundColor: 'transparent',
      tooltip: { trigger: 'item' },
      legend: { bottom: '0%', left: 'center', textStyle: { color: '#9ca3af', fontSize: 10 }, icon: 'circle' },
      series: [
         {
            name: t('dashboard.riskTitle'),
            type: 'pie',
            radius: ['40%', '70%'],
            center: ['50%', '45%'],
            avoidLabelOverlap: false,
            itemStyle: { borderRadius: 5, borderColor: '#1e1e1e', borderWidth: 2 },
            label: { show: false, position: 'center' },
            emphasis: { label: { show: true, fontSize: 14, fontWeight: 'bold', color: '#fff' } },
            labelLine: { show: false },
            data: data
         }
      ]
   }
   riskChart.setOption(option)
}

// Watchers
watch(locale, () => {
   // Re-render charts for language update
   nextTick(() => {
      myChart?.dispose()
      riskChart?.dispose()
      initCharts()
      updateMainChart()
      updateRiskChart()
   })
})

watch(() => systemStore.chartData, () => {
  updateMainChart()
}, { deep: true })

watch(() => systemStore.riskStats, () => {
   updateRiskChart()
}, { deep: true })

// Lifecycle
const resizeHandler = () => {
   myChart?.resize()
   riskChart?.resize()
}

onMounted(() => {
  initCharts()
  window.addEventListener('resize', resizeHandler)
  updateMainChart() // Initial draw
})

onUnmounted(() => {
  window.removeEventListener('resize', resizeHandler)
  myChart?.dispose()
  riskChart?.dispose()
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

/* Table overrides for dark mode blending */
:deep(.el-table th.el-table__cell) {
   background-color: rgba(30, 30, 30, 0.5) !important;
}
:deep(.el-table tr) {
   background-color: transparent !important;
}
:deep(.el-table td.el-table__cell), :deep(.el-table th.el-table__cell.is-leaf) {
   border-bottom: 1px solid #374151 !important;
}
:deep(.el-table--enable-row-hover .el-table__body tr:hover > td.el-table__cell) {
   background-color: rgba(255, 255, 255, 0.05) !important;
}
:deep(.el-table__inner-wrapper::before) {
   display: none;
}
</style>
