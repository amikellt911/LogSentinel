<template>
  <div class="bg-gray-800/30 border border-gray-700 rounded p-4 flex flex-col min-h-[300px]">
    <h3 class="text-xs font-bold text-gray-400 uppercase tracking-widest mb-2">
      {{ $t('dashboard.riskTitle') }}
    </h3>
    <div ref="chartRef" class="flex-1 w-full min-h-[250px]"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, nextTick } from 'vue'
import { useI18n } from 'vue-i18n'
import { useSystemStore } from '../stores/system'
import * as echarts from 'echarts'

const { t, locale } = useI18n()
const systemStore = useSystemStore()

// 图表 DOM 引用
const chartRef = ref<HTMLElement | null>(null)
let chart: echarts.ECharts | null = null

/**
 * 初始化 ECharts 风险分布饼图
 */
function initChart() {
  if (!chartRef.value) return

  // 初始化 ECharts 实例（深色主题，透明背景）
  // @ts-ignore
  chart = echarts.init(chartRef.value, 'dark', {
    renderer: 'canvas',
    backgroundColor: 'transparent'
  })

  updateChart()
}

/**
 * 更新图表数据
 */
function updateChart() {
  if (!chart) return

  // 构造饼图数据（从 store 获取风险统计数据）
  const data = [
    {
      value: systemStore.riskStats.Critical,
      name: 'Critical',
      itemStyle: { color: '#F56C6C' }
    },
    {
      value: systemStore.riskStats.Error,
      name: 'Error',
      itemStyle: { color: '#E6A23C' }
    },
    {
      value: systemStore.riskStats.Warning,
      name: 'Warning',
      itemStyle: { color: '#FAC858' }
    },
    {
      value: systemStore.riskStats.Info,
      name: 'Info',
      itemStyle: { color: '#409EFF' }
    },
    {
      value: systemStore.riskStats.Safe,
      name: 'Safe',
      itemStyle: { color: '#67C23A' }
    }
  ]

  // ECharts 配置项
  const option = {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',
      formatter: '{a} <br/>{b}: {c} ({d}%)' // 鼠标悬停显示格式
    },
    legend: {
      bottom: '0%',
      left: 'center',
      textStyle: {
        color: '#9ca3af',
        fontSize: 10
      },
      icon: 'circle'
    },
    series: [
      {
        name: t('dashboard.riskTitle'),
        type: 'pie',
        radius: ['40%', '70%'], // 环形饼图（内半径 40%，外半径 70%）
        center: ['50%', '45%'], // 图表中心位置
        avoidLabelOverlap: false, // 允许标签重叠
        itemStyle: {
          borderRadius: 5,
          borderColor: '#1e1e1e',
          borderWidth: 2
        },
        label: {
          show: false, // 默认不显示标签
          position: 'center'
        },
        emphasis: {
          // 鼠标悬停时显示标签
          label: {
            show: true,
            fontSize: 14,
            fontWeight: 'bold',
            color: '#fff'
          }
        },
        labelLine: {
          show: false
        },
        data: data
      }
    ]
  }

  chart.setOption(option)
}

/**
 * 监听语言变化，重新渲染图表
 */
watch(locale, () => {
  nextTick(() => {
    if (chart) {
      chart.dispose()
      initChart()
    }
  })
})

/**
 * 监听风险统计数据变化，更新图表
 */
watch(
  () => systemStore.riskStats,
  () => {
    updateChart()
  },
  { deep: true }
)

/**
 * 窗口大小变化时重新调整图表尺寸
 */
const resizeHandler = () => {
  chart?.resize()
}

onMounted(() => {
  initChart()
  window.addEventListener('resize', resizeHandler)
})

onUnmounted(() => {
  window.removeEventListener('resize', resizeHandler)
  chart?.dispose() // 销毁图表实例，释放内存
})
</script>
