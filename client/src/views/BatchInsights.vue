<template>
    <div class="h-full flex flex-col p-6 space-y-6 overflow-hidden relative bg-black/20">
      
      <!-- Header Section -->
      <div class="flex justify-between items-center">
          <div>
              <h2 class="text-xl font-bold text-gray-100 flex items-center gap-3">
                  <el-icon class="text-purple-400"><Cpu /></el-icon>
                  {{ $t('insights.title') }}
              </h2>
              <p class="text-gray-500 text-sm mt-1 font-mono">
                  {{ $t('insights.subtitle') }}
              </p>
          </div>
          <div class="flex items-center gap-4">
               <div class="flex items-center gap-2 text-xs font-mono bg-gray-900 px-3 py-1 rounded border border-gray-700">
                  <span class="w-2 h-2 rounded-full bg-green-500 animate-pulse"></span>
                  {{ $t('insights.lastBatch') }}: {{ lastBatchTime }}
               </div>
               <el-tag effect="dark" type="info" class="font-mono">{{ $t('insights.context') }}: 512 tokens</el-tag>
          </div>
      </div>
  
      <!-- Timeline Container -->
      <div class="flex-1 overflow-y-auto custom-scrollbar pr-4 relative">
          <!-- Connecting Line -->
          <div class="absolute left-[19px] top-4 bottom-0 w-0.5 bg-gray-800 z-0"></div>
  
          <div class="space-y-6 z-10 relative">
              <transition-group name="list">
                  <div v-for="batch in batches" :key="batch.id" class="flex gap-6 group">
                      <!-- Left: Time & Status Node -->
                      <div class="flex flex-col items-end w-32 shrink-0 pt-1">
                          <span class="font-mono text-gray-500 text-xs">{{ batch.time }}</span>
                          <div class="mt-2 w-10 h-10 rounded-full border-4 flex items-center justify-center bg-gray-900 relative z-10 transition-colors duration-500"
                               :class="getStatusBorderColor(batch.status)">
                              <el-icon :size="16" :class="getStatusTextColor(batch.status)">
                                  <component :is="batch.icon" />
                              </el-icon>
                          </div>
                          <span class="text-[10px] uppercase font-bold tracking-widest mt-1" :class="getStatusTextColor(batch.status)">
                              {{ batch.status }}
                          </span>
                      </div>
  
                      <!-- Right: Card -->
                      <div class="flex-1 bg-gray-800/40 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/60 transition-all duration-300 hover:border-gray-600 shadow-lg relative overflow-hidden">
                          <!-- Background Glow for High Risk -->
                          <div v-if="batch.status === 'CRITICAL'" class="absolute inset-0 bg-red-900/10 z-0 pointer-events-none animate-pulse"></div>
  
                          <div class="relative z-10">
                              <!-- Header -->
                              <div class="flex justify-between items-start mb-3 border-b border-gray-700/50 pb-2">
                                  <div>
                                      <h3 class="text-sm font-bold text-gray-200 flex items-center gap-2">
                                          {{ $t('insights.batch') }} #{{ batch.id }}
                                          <span class="px-1.5 py-0.5 rounded bg-gray-700 text-[10px] text-gray-400 font-normal">
                                              {{ $t('insights.size') }}: {{ batch.logCount }} {{ $t('insights.logs') }}
                                          </span>
                                      </h3>
                                  </div>
                                  <div class="flex gap-2">
                                      <span v-for="tag in batch.tags" :key="tag" 
                                            class="text-[10px] px-2 py-0.5 rounded-full border border-gray-600 text-gray-400 bg-gray-900/50">
                                          #{{ tag }}
                                      </span>
                                  </div>
                              </div>
  
                              <!-- Content -->
                              <div class="flex gap-4">
                                  <!-- Mini Stats -->
                                  <div class="w-24 shrink-0 flex flex-col gap-2 border-r border-gray-700/50 pr-4">
                                      <div class="text-center">
                                          <div class="text-xs text-gray-500 uppercase">{{ $t('insights.risks') }}</div>
                                          <div class="text-lg font-bold" :class="batch.riskCount > 0 ? 'text-red-400' : 'text-gray-400'">
                                              {{ batch.riskCount }}
                                          </div>
                                      </div>
                                      <div class="text-center">
                                          <div class="text-xs text-gray-500 uppercase">{{ $t('insights.latency') }}</div>
                                          <div class="text-sm font-mono text-blue-400">{{ batch.avgLatency }}ms</div>
                                      </div>
                                  </div>
  
                                  <!-- AI Text -->
                                  <div class="flex-1">
                                      <div class="text-xs text-purple-400 mb-1 font-bold uppercase tracking-wider flex items-center gap-1">
                                          <el-icon><MagicStick /></el-icon> {{ $t('insights.aiSummary') }}
                                      </div>
                                      <p class="text-gray-300 text-sm leading-relaxed font-mono type-writer-text">
                                          {{ batch.summary }}
                                      </p>
                                  </div>
                              </div>
                          </div>
                      </div>
                  </div>
              </transition-group>
          </div>
  
          <!-- Initial Placeholder -->
          <div v-if="batches.length === 0" class="flex justify-center items-center h-64 text-gray-600 font-mono animate-pulse">
              {{ $t('insights.waiting') }}
          </div>
      </div>
    </div>
  </template>
  
  <script setup lang="ts">
  import { ref, onMounted, onUnmounted } from 'vue'
  import { Cpu, MagicStick, Warning, CircleCheck, CircleClose } from '@element-plus/icons-vue'
  import dayjs from 'dayjs'
  import { useSystemStore } from '../stores/system'
  
  const systemStore = useSystemStore()
  
  interface BatchItem {
      id: number
      time: string
      status: 'NORMAL' | 'WARNING' | 'CRITICAL'
      logCount: number
      riskCount: number
      avgLatency: number
      summary: string
      tags: string[]
      icon: any
  }
  
  const batches = ref<BatchItem[]>([])
  const lastBatchTime = ref('--:--:--')
  let timer: number | null = null
  let batchCounter = 1024
  
  // --- Mock Data Generator ---
  
  const TEMPLATES_EN = [
      {
          status: 'NORMAL',
          summary: "Traffic patterns nominal. User authentication flows behaving as expected. No anomalies detected in the last 5s window.",
          tags: ['auth', 'nominal'],
          riskCount: 0
      },
      {
          status: 'NORMAL',
          summary: "Batch processing completed. Database query latency average is stable at 45ms. Ingress throughput steady.",
          tags: ['db', 'performance'],
          riskCount: 0
      },
      {
          status: 'WARNING',
          summary: "Detected slight elevation in 404 responses from /api/v1/user endpoints. Possible crawler activity or dead link ref.",
          tags: ['404', 'crawler'],
          riskCount: 2
      },
      {
          status: 'CRITICAL',
          summary: "SECURITY ALERT: Coordinated SQL Injection attempt detected across 3 distinct IPs targeting login controller. Payloads blocked.",
          tags: ['security', 'sqli', 'attack'],
          riskCount: 15
      },
      {
          status: 'WARNING',
          summary: "High memory usage detected in Worker #3. Garbage collection frequency increased. Suggest monitoring heap size.",
          tags: ['system', 'memory'],
          riskCount: 1
      }
  ]
  
  const TEMPLATES_ZH = [
      {
          status: 'NORMAL',
          summary: "流量模式正常。用户认证流程运行符合预期。过去 5 秒窗口内未检测到异常。",
          tags: ['认证', '正常'],
          riskCount: 0
      },
      {
          status: 'NORMAL',
          summary: "批处理完成。数据库查询平均延迟稳定在 45ms。入口吞吐量平稳。",
          tags: ['数据库', '性能'],
          riskCount: 0
      },
      {
          status: 'WARNING',
          summary: "检测到 /api/v1/user 端点的 404 响应略有升高。可能是爬虫活动或死链引用。",
          tags: ['404', '爬虫'],
          riskCount: 2
      },
      {
          status: 'CRITICAL',
          summary: "安全警报：检测到针对登录控制器的协同 SQL 注入尝试，涉及 3 个不同 IP。攻击载荷已拦截。",
          tags: ['安全', 'SQL注入', '攻击'],
          riskCount: 15
      },
      {
          status: 'WARNING',
          summary: "检测到 Worker #3 内存使用率过高。垃圾回收频率增加。建议监控堆大小。",
          tags: ['系统', '内存'],
          riskCount: 1
      }
  ]
  
  function generateBatch() {
      batchCounter++
      const now = dayjs()
      lastBatchTime.value = now.format('HH:mm:ss')
  
      // Randomly pick a template, weighted towards Normal
      const r = Math.random()
      let tplIndex = 0
      if (r > 0.90) tplIndex = 3 // Critical
      else if (r > 0.70) tplIndex = 2 // Warning
      else if (r > 0.60) tplIndex = 4 // Warning (System)
      else tplIndex = Math.floor(Math.random() * 2) // Normal
  
      // Switch template set based on current language
      const templates = systemStore.settings.general.language === 'zh' ? TEMPLATES_ZH : TEMPLATES_EN
      const tpl = templates[tplIndex]
      
      const logs = Math.floor(Math.random() * 40) + 10
  
      const batch: BatchItem = {
          id: batchCounter,
          time: now.format('HH:mm:ss'),
          status: tpl.status as any,
          logCount: logs,
          riskCount: tpl.riskCount,
          avgLatency: Math.floor(Math.random() * 50) + 20,
          summary: tpl.summary,
          tags: tpl.tags,
          icon: tpl.status === 'CRITICAL' ? CircleClose : (tpl.status === 'WARNING' ? Warning : CircleCheck)
      }
  
      // Add to top
      batches.value.unshift(batch)
      
      // Keep list manageable
      if (batches.value.length > 20) {
          batches.value.pop()
      }
  }
  
  function getStatusBorderColor(status: string) {
      switch(status) {
          case 'CRITICAL': return 'border-red-500 bg-red-900/20'
          case 'WARNING': return 'border-yellow-500 bg-yellow-900/20'
          default: return 'border-green-500 bg-green-900/20'
      }
  }
  
  function getStatusTextColor(status: string) {
      switch(status) {
          case 'CRITICAL': return 'text-red-500'
          case 'WARNING': return 'text-yellow-500'
          default: return 'text-green-500'
      }
  }
  
  onMounted(() => {
      // Initial seed
      generateBatch()
      
      // Auto generate
      // @ts-ignore
      timer = setInterval(generateBatch, 4000)
  })
  
  onUnmounted(() => {
      if (timer) clearInterval(timer)
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
  
  /* List Transition Animation */
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
  