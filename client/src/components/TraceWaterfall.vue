<template>
  <div class="w-full h-full flex flex-col">
    <div class="flex justify-between items-center mb-2 px-4 pt-4">
      <h3 class="text-sm font-bold text-gray-400 uppercase tracking-widest">
        {{ $t('traceExplorer.waterfall.title') }}
      </h3>
      <div class="flex gap-2">
        <div class="flex items-center gap-1">
          <div class="w-3 h-3 bg-[#67C23A] rounded-sm"></div>
          <span class="text-xs text-gray-400">Success</span>
        </div>
        <div class="flex items-center gap-1">
          <div class="w-3 h-3 bg-[#F56C6C] rounded-sm"></div>
          <span class="text-xs text-gray-400">Error</span>
        </div>
      </div>
    </div>

    <div ref="chartRef" class="flex-1 min-h-[400px] w-full overflow-hidden"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, nextTick, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import * as echarts from 'echarts'
import type { TraceSpan } from '../types/trace'

const { t } = useI18n()

interface Props {
  spans: TraceSpan[]
}

const props = defineProps<Props>()
const chartRef = ref<HTMLElement>()
// 使用 shallowRef 避免 ECharts 实例被 Vue 深度代理导致性能问题
const chartInstance = shallowRef<echarts.ECharts | null>(null)
let resizeObserver: ResizeObserver | null = null

/**
 * 核心修复 1：将 Y 轴逻辑改为"按服务合并"（泳道图）
 * 同一个服务的所有请求都会落在同一行
 */
function getSwimlaneData() {
  // 1. 还是按时间排序，确保渲染顺序
  const list = [...props.spans].sort((a, b) => a.start_time - b.start_time)

  // 2. 提取唯一的服务名列表 (去重)
  // Set 会自动去重，Array.from 转回数组
  const uniqueServices = Array.from(new Set(list.map(s => s.service_name)))

  // 3. 构建 Y 轴标签 (只显示服务名)
  const yAxisData = uniqueServices

  // 4. 创建一个映射表：服务名 -> Y轴索引 (0, 1, 2...)
  const serviceIndexMap: Record<string, number> = {}
  uniqueServices.forEach((name, index) => {
    serviceIndexMap[name] = index
  })

  // 5. 计算最大时间
  const maxTime = Math.max(...list.map(s => s.start_time + s.duration)) * 1.05

  return { list, maxTime, yAxisData, serviceIndexMap }
}

function initChart() {
  if (!chartRef.value) return

  chartInstance.value = echarts.init(chartRef.value)
  updateChart()

  // 核心修复 2：使用 ResizeObserver 替代 window.resize
  // 解决 Drawer 打开动画过程中 ECharts 获取不到正确宽度的问题
  resizeObserver = new ResizeObserver(() => {
    chartInstance.value?.resize()
  })
  resizeObserver.observe(chartRef.value)
}

function updateChart() {
  if (!chartInstance.value || !props.spans.length) return

  const { list, maxTime, yAxisData, serviceIndexMap } = getSwimlaneData()

  const option: echarts.EChartsOption = {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',
      backgroundColor: 'rgba(30, 30, 30, 0.95)',
      borderColor: '#374151',
      textStyle: { color: '#e5e7eb', fontSize: 12 },
      formatter: (params: any) => {
        // dataIndex 对应 list 的索引，所以这里可以直接取到 span
        const span = list[params.dataIndex]
        return renderTooltip(span)
      }
    },
    grid: {
      left: 16,
      right: 16,
      top: 40,    // 顶部留出时间轴高度
      bottom: 30, // 底部留出滚动条高度
      containLabel: true
    },
    xAxis: {
      type: 'value',
      position: 'top',
      axisLabel: { color: '#9ca3af', fontFamily: 'monospace' },
      splitLine: { lineStyle: { color: '#374151', type: 'dashed', opacity: 0.3 } },
      max: maxTime,
      // 强制显示最小刻度，防止缩放过大时消失
      minInterval: 1
    },
    yAxis: {
      type: 'category',
      data: yAxisData,
      inverse: true,
      axisLine: { show: false },
      axisTick: { show: false },
      axisLabel: {
        color: '#9ca3af',
        fontFamily: 'monospace',
        formatter: (value: string) => {
          return value.length > 20 ? value.substring(0, 20) + '...' : value
        }
      }
    },
    // --- 核心修改区域 Start ---
    dataZoom: [
      // 1. X 轴滑块 (底部拖动条)
      {
        type: 'slider',
        xAxisIndex: 0, // 控制时间轴
        filterMode: 'weakFilter', // 关键！确保跨越视图的 Span 不会因为部分在视图外而被隐藏
        height: 16,
        bottom: 5,
        borderColor: 'transparent',
        backgroundColor: 'rgba(55, 65, 81, 0.1)',
        fillerColor: 'rgba(64, 158, 255, 0.15)',
        handleStyle: {
          color: '#409eff',
          opacity: 0.8
        },
        textStyle: { color: 'transparent' }, // 不显示滑块两边的文字，保持界面整洁
        showDataShadow: false, // 不显示背景数据阴影，提升性能
      },
      // 2. X 轴鼠标缩放 (滚轮缩放 / 鼠标拖拽平移)
      {
        type: 'inside',
        xAxisIndex: 0, // 控制时间轴
        filterMode: 'weakFilter',
        zoomOnMouseWheel: true, // 允许滚轮缩放时间
        moveOnMouseWheel: false, // 禁止滚轮平移(防止和垂直滚动冲突)
        moveOnMouseMove: true,   // 允许按住鼠标左键拖拽平移
      },
      // 3. Y 轴滑块 (右侧，仅当服务很多时才需要)
      {
        type: 'slider',
        yAxisIndex: 0,
        width: 16,
        right: 0,
        show: yAxisData.length > 10, // 只有超过10个服务才显示垂直滚动条
        filterMode: 'empty',
        borderColor: 'transparent',
        backgroundColor: 'rgba(55, 65, 81, 0.1)',
        fillerColor: 'rgba(64, 158, 255, 0.15)',
      },
      // 4. Y 轴鼠标滚轮 (只用于上下滚动，不缩放)
      {
        type: 'inside',
        yAxisIndex: 0,
        zoomOnMouseWheel: false, // 禁止缩放 Y 轴高度
        moveOnMouseWheel: true,  // 允许滚轮上下查看服务
        moveOnMouseMove: false   // 禁止拖拽 Y 轴
      }
    ],
    // --- 核心修改区域 End ---
    series: [
      {
        type: 'custom',
        renderItem: (params: any, api: any) => {
          const categoryIndex = api.value(0)
          const start = api.value(1)
          const duration = api.value(2)
          const status = api.value(3)

          const startPoint = api.coord([start, categoryIndex])
          const endPoint = api.coord([start + duration, categoryIndex])

          // 计算高度，稍微设高一点，因为现在行数少了
          const height = api.size([0, 1])[1] * 0.6
          const width = endPoint[0] - startPoint[0]
          const rectWidth = Math.max(width, 2)

          // 性能优化：如果矩形在视图外，renderItem 其实还是会计算，
          // 但 ECharts 的 clipRect 会处理它。
          // 只要 dataZoom 的 filterMode 是 weakFilter，这里就能正确渲染跨视图的矩形。

          return {
            type: 'rect',
            shape: {
              x: startPoint[0],
              y: startPoint[1] - height / 2,
              width: rectWidth,
              height: height
            },
            style: {
              fill: getStatusColor(status),
              opacity: 0.9,
              rx: 2
            }
          }
        },
        encode: {
          x: [1, 2],
          y: 0
        },
        // 修改 2: 数据映射
        // 关键点：将 span.service_name 转换为对应的 Y 轴索引 (serviceIndexMap)
        data: list.map((span) => [
          serviceIndexMap[span.service_name], // [0] 对应 Y 轴的索引 (Service Index)
          span.start_time,                    // [1] Start
          span.duration,                      // [2] Duration
          span.status                         // [3] Status
        ])
      }
    ]
  }

  chartInstance.value.setOption(option)
}

function getStatusColor(status: string): string {
  switch (status) {
    case 'error': return '#F56C6C'
    case 'warning': return '#E6A23C'
    default: return '#67C23A'
  }
}

function renderTooltip(span: TraceSpan) {
  return `
    <div class="p-2 min-w-[200px]">
      <div class="font-bold border-b border-gray-600 pb-1 mb-2 text-gray-200">
        ${span.service_name}
      </div>
      <div class="space-y-1 text-xs font-mono text-gray-300">
         <div class="flex justify-between"><span class="text-gray-500">${t('traceExplorer.waterfall.spanId')}:</span> <span class="text-blue-400">${span.span_id}</span></div>
         <div class="flex justify-between"><span class="text-gray-500">${t('traceExplorer.waterfall.operation')}:</span> <span>${span.operation || 'N/A'}</span></div>
         <div class="flex justify-between"><span class="text-gray-500">${t('traceExplorer.waterfall.startTime')}:</span> <span>${span.start_time}ms</span></div>
         <div class="flex justify-between"><span class="text-gray-500">${t('traceExplorer.waterfall.duration')}:</span> <span class="text-white font-bold">${span.duration}ms</span></div>
         <div class="flex justify-between"><span class="text-gray-500">${t('traceExplorer.waterfall.status')}:</span> <span style="color:${getStatusColor(span.status)}">${span.status}</span></div>
      </div>
    </div>
  `
}

// 监听 props 变化，使用 nextTick 确保 DOM 更新
watch(() => props.spans, () => {
  nextTick(() => {
    updateChart()
  })
}, { deep: true })

onMounted(() => {
  nextTick(() => {
    initChart()
  })
})

onUnmounted(() => {
  if (resizeObserver && chartRef.value) {
    resizeObserver.unobserve(chartRef.value)
  }
  chartInstance.value?.dispose()
})
</script>

<style scoped>
/* ECharts 容器样式由 ECharts 自动处理 */
</style>
