<template>
  <div class="h-full flex flex-col p-6 space-y-6">

    <!-- 顶部: 核心指标卡片 -->
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
      <!-- 1. 总日志处理数 -->
      <MetricCard
        :title="$t('dashboard.totalLogs')"
        :value="stats.total_logs"
        :icon="Document"
        trend="+5%"
        trend-type="up"
      />

      <!-- 2. 延迟 (排队时间) -->
      <MetricCard
        :title="$t('dashboard.latency')"
        :value="stats.avg_latency.toFixed(1)"
        unit="ms"
        :icon="Timer"
        :status="latencyStatus"
      />

      <!-- 3. AI 吞吐量 -->
      <MetricCard
        :title="$t('dashboard.aiRate')"
        :value="stats.ai_rate.toFixed(1)"
        unit="req/s"
        :icon="Cpu"
        status="info"
      />

      <!-- 4. 背压状态 -->
      <MetricCard
        :title="$t('dashboard.backpressure')"
        :value="stats.backpressure ? $t('dashboard.throttling') : $t('dashboard.normal')"
        :icon="Odometer"
        :status="stats.backpressure ? 'warning' : 'success'"
      />
    </div>

    <!-- 底部: 实时吞吐量趋势图 -->
    <div class="flex-1 bg-gray-800 rounded-lg p-4 shadow-lg border border-gray-700 flex flex-col min-h-0">
       <h3 class="text-gray-400 text-sm font-semibold mb-2 flex items-center gap-2">
          <el-icon><TrendCharts /></el-icon> {{ $t('dashboard.throughputTrend') }}
       </h3>
       <div class="flex-1 w-full" ref="lineChartEl"></div>
    </div>

  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue'
import * as echarts from 'echarts'
import { Document, Timer, Cpu, Odometer, TrendCharts } from '@element-plus/icons-vue'
import MetricCard from '../components/MetricCard.vue'
import { useSystemStore } from '../stores/system'
import { useI18n } from 'vue-i18n'

// @ts-ignore
const { t } = useI18n()
const systemStore = useSystemStore()

// -- 图表 DOM 引用 --
const lineChartEl = ref<HTMLElement | null>(null)
let lineChart: echarts.ECharts | null = null

// -- 状态计算 --
const stats = computed(() => {
    // Determine last AI Rate from chart history if available
    let currentAiRate = 0
    if (systemStore.chartData && systemStore.chartData.length > 0) {
        currentAiRate = systemStore.chartData[systemStore.chartData.length - 1].aiRate
    }

    return {
        total_logs: systemStore.totalLogsProcessed,
        avg_latency: systemStore.netLatency,
        ai_rate: currentAiRate,
        backpressure: systemStore.backpressureStatus !== 'Normal',
        current_qps: systemStore.chartData && systemStore.chartData.length > 0 ? systemStore.chartData[systemStore.chartData.length - 1].qps : 0
    }
})

const latencyStatus = computed(() => {
    if (stats.value.avg_latency > 500) return 'danger'
    if (stats.value.avg_latency > 200) return 'warning'
    return 'success'
})

// -- ECharts 逻辑 --

// 模拟实时数据
const lineDataX = ref<string[]>([])
const lineDataIngest = ref<number[]>([])
const lineDataAI = ref<number[]>([])

const initLineChart = () => {
    if (!lineChartEl.value) return
    lineChart = echarts.init(lineChartEl.value)
    // 填充初始数据
    const now = new Date()
    for(let i=0; i<20; i++) {
        const t = new Date(now.getTime() - (20-i)*2000)
        lineDataX.value.push(t.toLocaleTimeString())
        lineDataIngest.value.push(Math.floor(Math.random() * 50) + 100)
        lineDataAI.value.push(Math.floor(Math.random() * 20) + 30)
    }
    updateLineOption()
}

const updateLineOption = () => {
    if (!lineChart) return
    const option = {
        tooltip: { trigger: 'axis' },
        legend: { data: ['Ingest Rate', 'AI Rate'], textStyle: { color: '#9CA3AF' } },
        grid: { left: '3%', right: '4%', bottom: '3%', top: '15%', containLabel: true },
        xAxis: {
            type: 'category',
            boundaryGap: false,
            data: lineDataX.value,
            axisLabel: { color: '#6B7280' },
            axisLine: { lineStyle: { color: '#374151' } }
        },
        yAxis: {
            type: 'value',
            axisLabel: { color: '#6B7280' },
            splitLine: { lineStyle: { color: '#374151' } }
        },
        series: [
            {
                name: 'Ingest Rate',
                type: 'line',
                smooth: true,
                data: lineDataIngest.value,
                itemStyle: { color: '#3B82F6' },
                areaStyle: { color: new echarts.graphic.LinearGradient(0,0,0,1, [{offset:0, color:'rgba(59,130,246,0.5)'}, {offset:1, color:'rgba(59,130,246,0.0)'}]) }
            },
            {
                name: 'AI Rate',
                type: 'line',
                smooth: true,
                data: lineDataAI.value,
                itemStyle: { color: '#8B5CF6' },
                areaStyle: { color: new echarts.graphic.LinearGradient(0,0,0,1, [{offset:0, color:'rgba(139,92,246,0.5)'}, {offset:1, color:'rgba(139,92,246,0.0)'}]) }
            }
        ],
        backgroundColor: 'transparent'
    }
    lineChart.setOption(option)
}

// -- 定时更新图表 --
let timer: number | null = null

onMounted(() => {
    initLineChart()

    // 监听窗口大小变化
    window.addEventListener('resize', handleResize)

    // 开始更新循环
    // @ts-ignore
    timer = setInterval(() => {
        // 更新数据
        const now = new Date()
        lineDataX.value.push(now.toLocaleTimeString())
        // 模拟 QPS 波动
        const baseIngest = stats.value.current_qps || 120
        lineDataIngest.value.push(baseIngest + Math.floor(Math.random()*20 - 10))

        const baseAI = stats.value.ai_rate || 35
        lineDataAI.value.push(baseAI + Math.floor(Math.random()*10 - 5))

        // 保持队列长度
        if(lineDataX.value.length > 20) {
            lineDataX.value.shift()
            lineDataIngest.value.shift()
            lineDataAI.value.shift()
        }

        updateLineOption()
    }, 2000)
})

onUnmounted(() => {
    if (timer) clearInterval(timer)
    window.removeEventListener('resize', handleResize)
    lineChart?.dispose()
})

const handleResize = () => {
    lineChart?.resize()
}

</script>
