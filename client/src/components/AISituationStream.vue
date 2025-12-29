<template>
  <div class="h-full flex flex-col p-6 space-y-4 overflow-hidden relative bg-black/20">
    <!-- 顶部控制栏 -->
    <div class="flex justify-between items-center">
      <div>
        <h2 class="text-xl font-bold text-gray-100 flex items-center gap-3">
          <el-icon class="text-purple-400"><MagicStick /></el-icon>
          {{ $t('serviceMonitor.aiStream.title') }}
        </h2>
        <p class="text-gray-500 text-sm mt-1 font-mono">
          {{ $t('serviceMonitor.aiStream.subtitle') }}
        </p>
      </div>
      <div class="flex items-center gap-4">
        <!-- 暂停/继续按钮 -->
        <el-button
          :type="isPaused ? 'success' : 'warning'"
          :icon="isPaused ? VideoPlay : VideoPause"
          @click="toggleStream"
          size="small"
        >
          {{ isPaused ? $t('serviceMonitor.aiStream.resume') : $t('serviceMonitor.aiStream.pause') }}
        </el-button>

        <!-- 最后批次时间 -->
        <div class="flex items-center gap-2 text-xs font-mono bg-gray-900 px-3 py-1 rounded border border-gray-700">
          <span class="w-2 h-2 rounded-full" :class="isPaused ? 'bg-yellow-500' : 'bg-green-500 animate-pulse'"></span>
          {{ $t('insights.lastBatch') }}: {{ lastBatchTime }}
        </div>

        <!-- 列表数量统计 -->
        <el-tag effect="dark" type="info" class="font-mono">
          {{ batches.length }}/{{ MAX_BATCHES }}
        </el-tag>
      </div>
    </div>

    <!-- Timeline Container -->
    <div class="flex-1 overflow-y-auto custom-scrollbar pr-4 relative">
      <!-- Connecting Line（时间线连接线） -->
      <div class="absolute left-[19px] top-4 bottom-0 w-0.5 bg-gray-800 z-0"></div>

      <div class="space-y-6 z-10 relative">
        <transition-group name="list">
          <div v-for="batch in batches" :key="batch.id" class="flex gap-6 group">
            <!-- 左侧：时间 + 状态节点 -->
            <div class="flex flex-col items-end w-32 shrink-0 pt-1">
              <span class="font-mono text-gray-500 text-xs">{{ batch.time }}</span>
              <div
                class="mt-2 w-10 h-10 rounded-full border-4 flex items-center justify-center bg-gray-900 relative z-10 transition-colors duration-500"
                :class="getStatusBorderColor(batch.status)"
              >
                <el-icon :size="16" :class="getStatusTextColor(batch.status)">
                  <component :is="batch.icon" />
                </el-icon>
              </div>
              <span
                class="text-[10px] uppercase font-bold tracking-widest mt-1"
                :class="getStatusTextColor(batch.status)"
              >
                {{ batch.status }}
              </span>
            </div>

            <!-- 右侧：卡片 -->
            <div
              class="flex-1 bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg relative overflow-hidden"
            >
              <!-- 高风险的背景光晕效果 -->
              <div
                v-if="batch.status === 'CRITICAL'"
                class="absolute inset-0 bg-red-900/10 z-0 pointer-events-none animate-pulse"
              ></div>

              <div class="relative z-10">
                <!-- 卡片头部 -->
                <div class="flex justify-between items-start mb-3 border-b border-gray-700/50 pb-2">
                  <div>
                    <h3 class="text-sm font-bold text-gray-200 flex items-center gap-2 flex-wrap">
                      {{ $t('insights.batch') }} #{{ batch.id }}
                      <span class="px-1.5 py-0.5 rounded bg-gray-700 text-[10px] text-gray-400 font-normal">
                        {{ $t('insights.size') }}: {{ batch.logCount }} {{ $t('insights.logs') }}
                      </span>
                      <!-- trace_id 显示 -->
                      <span class="px-1.5 py-0.5 rounded bg-blue-900/30 text-[10px] text-blue-400 font-mono font-normal border border-blue-700">
                        trace_id: {{ batch.traceId }}
                      </span>
                    </h3>
                  </div>
                  <div class="flex gap-2">
                    <span
                      v-for="tag in batch.tags"
                      :key="tag"
                      class="text-[10px] px-2 py-0.5 rounded-full border border-gray-600 text-gray-400 bg-gray-900/50"
                    >
                      #{{ tag }}
                    </span>
                  </div>
                </div>

                <!-- 卡片内容：AI 分析总结 -->
                <div>
                  <div class="text-xs text-purple-400 mb-2 font-bold uppercase tracking-wider flex items-center gap-1">
                    <el-icon><MagicStick /></el-icon> {{ $t('insights.aiSummary') }}
                  </div>
                  <p class="text-gray-300 text-sm leading-relaxed font-mono">
                    {{ batch.summary }}
                  </p>
                </div>
              </div>
            </div>
          </div>
        </transition-group>
      </div>

      <!-- 初始占位符 -->
      <div v-if="batches.length === 0" class="flex justify-center items-center h-64 text-gray-600 font-mono animate-pulse">
        {{ $t('insights.waiting') }}
      </div>

      <!-- 加载更多按钮（显示在底部） -->
      <div v-if="batches.length > 0" class="flex justify-center py-4">
        <el-button @click="loadMore" :loading="isLoading" text>
          {{ $t('serviceMonitor.aiStream.loadMore') }}
        </el-button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { MagicStick, Warning, CircleCheck, CircleClose, VideoPlay, VideoPause } from '@element-plus/icons-vue'
import dayjs from 'dayjs'
import { useSystemStore } from '../stores/system'

const systemStore = useSystemStore()

/**
 * 批次数据项接口
 */
interface BatchItem {
  id: number
  traceId: string // trace_id（等同于 batch_id）
  time: string
  status: 'NORMAL' | 'WARNING' | 'CRITICAL'
  logCount: number
  summary: string
  tags: string[]
  icon: any
}

// 响应式状态
const MAX_BATCHES = 100 // 列表最大数量
const batches = ref<BatchItem[]>([])
const lastBatchTime = ref('--:--:--')
const isPaused = ref(false) // 暂停状态
const isLoading = ref(false) // 加载状态
let timer: number | null = null
let batchCounter = 1024 // 新消息计数器（递增）
let minBatchId = 1024 // 记录当前列表中最小的批次号，用于加载历史

// --- Mock 数据模板 ---

const TEMPLATES_EN = [
  {
    status: 'NORMAL',
    summary: "Traffic patterns nominal. User authentication flows behaving as expected. No anomalies detected in the last 5s window.",
    tags: ['auth', 'nominal']
  },
  {
    status: 'NORMAL',
    summary: "Batch processing completed. Database query latency average is stable at 45ms. Ingress throughput steady.",
    tags: ['db', 'performance']
  },
  {
    status: 'WARNING',
    summary: "Detected slight elevation in 404 responses from /api/v1/user endpoints. Possible crawler activity or dead link ref.",
    tags: ['404', 'crawler']
  },
  {
    status: 'CRITICAL',
    summary: "SECURITY ALERT: Coordinated SQL Injection attempt detected across 3 distinct IPs targeting login controller. Payloads blocked.",
    tags: ['security', 'sqli', 'attack']
  },
  {
    status: 'WARNING',
    summary: "High memory usage detected in Worker #3. Garbage collection frequency increased. Suggest monitoring heap size.",
    tags: ['system', 'memory']
  }
]

const TEMPLATES_ZH = [
  {
    status: 'NORMAL',
    summary: "流量模式正常。用户认证流程运行符合预期。过去 5 秒窗口内未检测到异常。",
    tags: ['认证', '正常']
  },
  {
    status: 'NORMAL',
    summary: "批处理完成。数据库查询平均延迟稳定在 45ms。入口吞吐量平稳。",
    tags: ['数据库', '性能']
  },
  {
    status: 'WARNING',
    summary: "检测到 /api/v1/user 端点的 404 响应略有升高。可能是爬虫活动或死链引用。",
    tags: ['404', '爬虫']
  },
  {
    status: 'CRITICAL',
    summary: "安全警报：检测到针对登录控制器的协同 SQL 注入尝试，涉及 3 个不同 IP。攻击载荷已拦截。",
    tags: ['安全', 'SQL注入', '攻击']
  },
  {
    status: 'WARNING',
    summary: "检测到 Worker #3 内存使用率过高。垃圾回收频率增加。建议监控堆大小。",
    tags: ['系统', '内存']
  }
]

/**
 * 生成一个新的批次（Mock 数据）
 */
function generateBatch() {
  batchCounter++
  const now = dayjs()
  lastBatchTime.value = now.format('HH:mm:ss')

  // 随机选择一个模板（加权随机，偏向 Normal）
  const r = Math.random()
  let tplIndex = 0
  if (r > 0.90) tplIndex = 3 // Critical
  else if (r > 0.70) tplIndex = 2 // Warning
  else if (r > 0.60) tplIndex = 4 // Warning (System)
  else tplIndex = Math.floor(Math.random() * 2) // Normal

  // 根据当前语言切换模板集
  const templates = systemStore.settings.general.language === 'zh' ? TEMPLATES_ZH : TEMPLATES_EN
  const tpl = templates[tplIndex]

  const logs = Math.floor(Math.random() * 40) + 10

  // 生成 trace_id（模拟格式：trace_001234）
  const traceId = `trace_${String(batchCounter).padStart(6, '0')}`

  const batch: BatchItem = {
    id: batchCounter,
    traceId: traceId,
    time: now.format('HH:mm:ss'),
    status: tpl.status as any,
    logCount: logs,
    summary: tpl.summary,
    tags: tpl.tags,
    icon: tpl.status === 'CRITICAL' ? CircleClose : (tpl.status === 'WARNING' ? Warning : CircleCheck)
  }

  // 添加到顶部（最新消息在顶部）
  batches.value.unshift(batch)

  // 更新最小批次号（如果列表为空或新添加的批次号小于当前最小批次号）
  if (batches.value.length === 1 || batchCounter < minBatchId) {
    minBatchId = batchCounter
  }

  // 限制列表最大长度（保留最近 MAX_BATCHES 条）
  if (batches.value.length > MAX_BATCHES) {
    const removed = batches.value.pop()
    // 更新最小批次号（如果移除的是最小批次号）
    if (removed && removed.id === minBatchId) {
      const minId = Math.min(...batches.value.map(b => b.id))
      minBatchId = minId
    }
  }
}

/**
 * 切换暂停/继续状态
 * 暂停时只是停止定时器，取消暂停后继续生成新数据（不补回暂停期间遗漏的数据）
 */
function toggleStream() {
  isPaused.value = !isPaused.value

  if (isPaused.value) {
    // 暂停：清除定时器（不处理缓存，简化实现）
    if (timer) {
      clearInterval(timer)
      timer = null
    }
  } else {
    // 继续：重新启动定时器（继续生成新数据）
    // @ts-ignore
    timer = setInterval(generateBatch, 4000)
  }
}

/**
 * 加载更多历史批次（模拟）
 * 注意：加载的是更早的历史批次（批次号更小）
 */
function loadMore() {
  if (isLoading.value) return

  isLoading.value = true

  // TODO: Replace with real API
  // TODO: 替换为真实 API 调用
  // 模拟异步加载
  setTimeout(() => {
    // 生成 5 个历史批次（批次号递减，添加到列表底部）
    for (let i = 0; i < 5; i++) {
      minBatchId-- // 历史批次号递减
      const tplIndex = Math.floor(Math.random() * 5)
      const templates = systemStore.settings.general.language === 'zh' ? TEMPLATES_ZH : TEMPLATES_EN
      const tpl = templates[tplIndex]

      const logs = Math.floor(Math.random() * 40) + 10
      const traceId = `trace_${String(minBatchId).padStart(6, '0')}`

      const batch: BatchItem = {
        id: minBatchId,
        traceId: traceId,
        time: dayjs().subtract(i + 1, 'minute').format('HH:mm:ss'),
        status: tpl.status as any,
        logCount: logs,
        summary: tpl.summary,
        tags: tpl.tags,
        icon: tpl.status === 'CRITICAL' ? CircleClose : (tpl.status === 'WARNING' ? Warning : CircleCheck)
      }

      batches.value.push(batch)
    }

    isLoading.value = false
  }, 500)
}

/**
 * 根据状态返回边框颜色类名
 */
function getStatusBorderColor(status: string): string {
  switch (status) {
    case 'CRITICAL':
      return 'border-red-500 bg-red-900/20'
    case 'WARNING':
      return 'border-yellow-500 bg-yellow-900/20'
    default:
      return 'border-green-500 bg-green-900/20'
  }
}

/**
 * 根据状态返回文本颜色类名
 */
function getStatusTextColor(status: string): string {
  switch (status) {
    case 'CRITICAL':
      return 'text-red-500'
    case 'WARNING':
      return 'text-yellow-500'
    default:
      return 'text-green-500'
  }
}

// 生命周期钩子
onMounted(() => {
  // 初始种子数据
  generateBatch()

  // 自动生成批次（每 4 秒）
  // @ts-ignore
  timer = setInterval(generateBatch, 4000)
})

onUnmounted(() => {
  if (timer) clearInterval(timer)
})
</script>

<style scoped>
/* 自定义滚动条样式 */
.custom-scrollbar::-webkit-scrollbar {
  width: 6px;
}

.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #374151;
  border-radius: 3px;
}

/* 列表过渡动画 */
.list-enter-active,
.list-leave-active {
  transition: all 0.5s ease;
}

.list-enter-from {
  opacity: 0;
  transform: translateY(-30px);
}

.list-leave-to {
  opacity: 0;
  transform: translateY(30px);
}
</style>
