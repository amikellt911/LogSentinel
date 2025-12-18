<template>
  <div class="h-full flex flex-col p-6 overflow-hidden relative">

    <!-- Top Control Bar -->
    <div class="bg-gray-800/50 border border-gray-700 rounded p-4 mb-6 flex items-center gap-6 shrink-0">
       <div class="flex items-center gap-2">
          <span class="text-gray-400 text-xs uppercase font-bold">{{ $t('benchmark.tool') }}</span>
          <el-input model-value="wrk" readonly size="small" class="w-24 font-mono text-center" />
       </div>
       <div class="flex items-center gap-2">
          <span class="text-gray-400 text-xs uppercase font-bold">{{ $t('benchmark.threads') }}</span>
          <el-input v-model="params.threads" size="small" class="w-20 font-mono text-center" />
       </div>
       <div class="flex items-center gap-2">
          <span class="text-gray-400 text-xs uppercase font-bold">{{ $t('benchmark.connections') }}</span>
          <el-input v-model="params.connections" size="small" class="w-20 font-mono text-center" />
       </div>
       <div class="flex items-center gap-2">
          <span class="text-gray-400 text-xs uppercase font-bold">{{ $t('benchmark.duration') }}</span>
          <el-input v-model="params.duration" size="small" class="w-20 font-mono text-center" />
       </div>
    </div>

    <!-- Main Comparison Arena -->
    <div class="flex-1 flex gap-4 min-h-0 relative">

       <!-- Left Column: Python -->
       <div
         class="flex-1 bg-gray-800/30 border rounded-lg p-6 flex flex-col transition-all duration-300 relative overflow-hidden"
         :class="getCardClass('python')"
         @click="selectSide('python')"
         data-testid="python-card"
       >
          <!-- Header -->
          <div class="flex items-center justify-between border-b border-gray-700 pb-4 mb-4">
             <h3 class="text-xl font-bold text-gray-200">Python (FastAPI)</h3>
             <div
               class="w-6 h-6 rounded-full border-2 flex items-center justify-center transition-colors"
               :class="selection === 'python' ? 'border-green-500 bg-green-500/20' : 'border-gray-600'"
             >
                <div v-if="selection === 'python'" class="w-3 h-3 rounded-full bg-green-500"></div>
             </div>
          </div>

          <!-- Metrics Grid -->
          <div class="flex-1 space-y-4">
             <div v-for="metric in metrics" :key="metric.key" class="flex justify-between items-center p-3 bg-gray-900/50 rounded hover:bg-gray-900/80 transition-colors">
                <span class="text-gray-400 font-medium">{{ $t(`benchmark.metrics.${metric.key}`) }}</span>
                <span class="font-mono text-xl text-blue-400 font-bold">
                   {{ pythonData[metric.key] || '--' }} <span v-if="pythonData[metric.key]" class="text-xs text-gray-600 ml-1">{{ metric.unit }}</span>
                </span>
             </div>
          </div>

          <!-- Loading Overlay -->
          <div v-if="isRunning && selection === 'python'" class="absolute inset-0 bg-black/60 backdrop-blur-sm flex flex-col items-center justify-center z-20">
             <el-icon class="is-loading text-green-500 mb-4" :size="48"><Loading /></el-icon>
             <span class="text-green-400 font-mono tracking-widest animate-pulse">{{ $t('benchmark.running') }}</span>
             <span class="text-gray-500 text-xs mt-2 font-mono">{{ timeLeft }}s remaining</span>
          </div>
       </div>

       <!-- Right Column: C++ -->
       <div
         class="flex-1 bg-gray-800/30 border rounded-lg p-6 flex flex-col transition-all duration-300 relative overflow-hidden"
         :class="getCardClass('cpp')"
         @click="selectSide('cpp')"
       >
          <!-- Header -->
          <div class="flex items-center justify-between border-b border-gray-700 pb-4 mb-4">
             <h3 class="text-xl font-bold text-gray-200">LogSentinel (C++ Reactor)</h3>
             <div
               class="w-6 h-6 rounded-full border-2 flex items-center justify-center transition-colors"
               :class="selection === 'cpp' ? 'border-green-500 bg-green-500/20' : 'border-gray-600'"
             >
                <div v-if="selection === 'cpp'" class="w-3 h-3 rounded-full bg-green-500"></div>
             </div>
          </div>

          <!-- Metrics Grid -->
          <div class="flex-1 space-y-4">
             <div v-for="metric in metrics" :key="metric.key" class="flex justify-between items-center p-3 bg-gray-900/50 rounded hover:bg-gray-900/80 transition-colors">
                <span class="text-gray-400 font-medium">{{ $t(`benchmark.metrics.${metric.key}`) }}</span>
                <span class="font-mono text-xl text-purple-400 font-bold">
                   {{ cppData[metric.key] || '--' }} <span v-if="cppData[metric.key]" class="text-xs text-gray-600 ml-1">{{ metric.unit }}</span>
                </span>
             </div>
          </div>

          <!-- Loading Overlay -->
          <div v-if="isRunning && selection === 'cpp'" class="absolute inset-0 bg-black/60 backdrop-blur-sm flex flex-col items-center justify-center z-20">
             <el-icon class="is-loading text-green-500 mb-4" :size="48"><Loading /></el-icon>
             <span class="text-green-400 font-mono tracking-widest animate-pulse">{{ $t('benchmark.running') }}</span>
             <span class="text-gray-500 text-xs mt-2 font-mono">{{ timeLeft }}s remaining</span>
          </div>
       </div>

    </div>

    <!-- Start Button (Fixed Bottom Right) -->
    <div class="absolute bottom-8 right-8 z-30">
       <button
         class="bg-green-600 hover:bg-green-500 text-white font-bold py-4 px-8 rounded-full shadow-lg transition-all transform hover:scale-105 active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed flex items-center gap-3"
         :disabled="isRunning || !selection"
         @click="startBenchmark"
       >
          <el-icon class="text-xl"><VideoPlay /></el-icon>
          {{ $t('benchmark.start') }}
       </button>
    </div>

  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onUnmounted } from 'vue'
import { Loading, VideoPlay } from '@element-plus/icons-vue'

const params = reactive({
   tool: 'wrk',
   threads: '12',
   connections: '400',
   duration: '30s'
})

const selection = ref<'python' | 'cpp' | null>(null)
const isRunning = ref(false)
const timeLeft = ref(30)
let timer: number | null = null

const pythonData = ref<any>({})
const cppData = ref<any>({})

const metrics = [
   { key: 'qps', unit: 'req/s' },
   { key: 'p50', unit: 'ms' },
   { key: 'p99', unit: 'ms' },
   { key: 'net_io', unit: 'ms' },
   { key: 'scheduler', unit: 'ms' },
   { key: 'total_logs', unit: '' },
   { key: 'total_ai', unit: '' }
]

function getCardClass(side: 'python' | 'cpp') {
   if (isRunning.value && selection.value !== side) return 'opacity-40 grayscale pointer-events-none border-gray-800'
   if (selection.value === side) return 'border-green-500/50 bg-gray-800/50 shadow-[0_0_20px_rgba(34,197,94,0.1)]'
   return 'border-gray-700 hover:border-gray-500 cursor-pointer'
}

function selectSide(side: 'python' | 'cpp') {
   if (isRunning.value) return
   selection.value = side
}

function startBenchmark() {
   if (!selection.value || isRunning.value) return

   isRunning.value = true
   timeLeft.value = 30

   // Clear previous data for selected side
   if (selection.value === 'python') pythonData.value = {}
   else cppData.value = {}

   // Countdown Timer
   // @ts-ignore
   timer = setInterval(() => {
      timeLeft.value--
      if (timeLeft.value <= 0) {
         finishBenchmark()
      }
   }, 1000) // Real 30s logic as requested
}

function finishBenchmark() {
   if (timer) clearInterval(timer)
   isRunning.value = false

   if (selection.value === 'python') {
      pythonData.value = {
         qps: '845',
         p50: '12.4',
         p99: '145.2',
         net_io: '2.1',
         scheduler: '5.5',
         total_logs: '25,350',
         total_ai: '250'
      }
   } else {
      cppData.value = {
         qps: '12,450',
         p50: '0.8',
         p99: '4.2',
         net_io: '0.1',
         scheduler: '0.05',
         total_logs: '373,500',
         total_ai: '3,800'
      }
   }
}

onUnmounted(() => {
   if (timer) clearInterval(timer)
})
</script>

<style scoped>
/* Ensure inputs blend in */
:deep(.el-input__wrapper) {
   background-color: rgba(0,0,0,0.3) !important;
   box-shadow: none !important;
   border: 1px solid #4b5563;
}
:deep(.el-input__inner) {
   color: #e5e7eb !important;
}
</style>
