<template>
   <div class="h-full flex flex-col p-6 space-y-4">
     <!-- Filter Bar -->
     <div class="flex flex-wrap gap-4 items-center bg-gray-800/30 p-4 rounded border border-gray-700">
       <div class="flex items-center gap-2">
         <label class="text-gray-400 text-sm">{{ $t('history.riskLevel') }}:</label>
         <el-select v-model="filterLevel" :placeholder="$t('history.all')" size="small" class="w-32" clearable>
           <el-option label="Critical" value="Critical" />
           <el-option label="Error" value="Error" />
           <el-option label="Warning" value="Warning" />
           <el-option label="Info" value="Info" />
         </el-select>
       </div>
 
       <div class="flex items-center gap-2 flex-1">
          <label class="text-gray-400 text-sm">{{ $t('history.search') }}:</label>
          <el-input 
             v-model="searchQuery" 
             :placeholder="$t('history.placeholder')" 
             size="small"
             clearable
             :prefix-icon="Search"
          />
       </div>
 
       <el-button type="primary" :icon="Refresh" size="small" @click="refreshLogs" :loading="loading">
          {{ $t('history.refresh') }}
       </el-button>
     </div>
 
     <!-- Data Table -->
     <div class="flex-1 overflow-hidden bg-gray-800/30 rounded border border-gray-700 relative">
       <el-table 
         :data="filteredLogs" 
         style="width: 100%; height: 100%;" 
         v-loading="loading"
         element-loading-background="rgba(0, 0, 0, 0.7)"
         :row-class-name="tableRowClassName"
       >
         <el-table-column prop="timestamp" :label="$t('history.table.time')" width="180" sortable />
         <el-table-column prop="level" :label="$t('history.table.level')" width="100">
           <template #default="{ row }">
              <span class="px-2 py-1 rounded text-xs font-bold" :class="getLevelBadgeClass(row.level)">
                 {{ row.level }}
              </span>
           </template>
         </el-table-column>
         <el-table-column prop="message" :label="$t('history.table.summary')" min-width="300" />
         <el-table-column prop="id" :label="$t('history.table.traceId')" width="220" />
         <el-table-column :label="$t('history.table.actions')" width="100" fixed="right">
           <template #default="{ row }">
              <el-button type="primary" link size="small" @click="showDetails(row)">
                 {{ $t('history.details') }}
              </el-button>
           </template>
         </el-table-column>
       </el-table>
     </div>
 
     <!-- Pagination -->
     <div class="flex justify-end mt-2">
        <el-pagination
          v-model:current-page="currentPage"
          v-model:page-size="pageSize"
          :page-sizes="[20, 50, 100]"
          layout="total, sizes, prev, pager, next"
          :total="totalLogs"
          @size-change="refreshLogs"
          @current-change="refreshLogs"
        />
     </div>
 
     <!-- Details Dialog -->
     <el-dialog v-model="dialogVisible" :title="$t('history.dialog.title')" width="60%">
        <div v-if="selectedLog" class="space-y-4">
           <div class="grid grid-cols-2 gap-4 text-sm">
              <div><span class="text-gray-400">{{ $t('history.table.traceId') }}:</span> {{ selectedLog.id }}</div>
              <div><span class="text-gray-400">{{ $t('history.table.time') }}:</span> {{ selectedLog.timestamp }}</div>
              <div><span class="text-gray-400">{{ $t('history.table.level') }}:</span> <span :class="getLevelTextClass(selectedLog.level)">{{ selectedLog.level }}</span></div>
           </div>
           
           <div class="bg-black p-4 rounded font-mono text-xs overflow-x-auto border border-gray-700">
              <div class="text-gray-500 mb-2"># {{ $t('history.dialog.aiSummary') }}</div>
              <div class="text-green-400">{{ selectedLog.message }}</div>
              
              <!-- Placeholder for raw log content if backend supports it later -->
              <div class="mt-4 text-gray-500 border-t border-gray-800 pt-2"># {{ $t('history.dialog.rawContent') }}</div>
              <div class="text-gray-300">GET /api/v1/user/123 HTTP/1.1 ...</div>
           </div>
        </div>
     </el-dialog>
   </div>
 </template>
 
 <script setup lang="ts">
 import { ref, computed, onMounted } from 'vue'
 import { Search, Refresh } from '@element-plus/icons-vue'
 import { useSystemStore, type LogEntry } from '../stores/system'
 import dayjs from 'dayjs'
 
 const systemStore = useSystemStore()
 const loading = ref(false)
 const filterLevel = ref('')
 const searchQuery = ref('')
 const currentPage = ref(1)
 const pageSize = ref(50)
 const totalLogs = ref(0)
 const dialogVisible = ref(false)
 const selectedLog = ref<LogEntry | null>(null)
 
 // In a real app, filtering happens on backend. 
 // For now, if we use the store's `logs` (which is limited size stream), we filter client side.
 // But the API `/api/history` is pagination ready. 
 // Let's implement real server-side pagination fetch here.
 
 const historyLogs = ref<LogEntry[]>([])
 
 const filteredLogs = computed(() => {
    // Client side filter for the currently fetched page (imperfect but better than nothing for demo)
    // Ideally backend handles filter.
    let res = historyLogs.value
    if (filterLevel.value) {
       res = res.filter(l => l.level === filterLevel.value || (filterLevel.value === 'Risk' && (l.level === 'RISK' || l.level === 'Critical')))
    }
    if (searchQuery.value) {
       const q = searchQuery.value.toLowerCase()
       res = res.filter(l => l.message.toLowerCase().includes(q) || String(l.id).toLowerCase().includes(q))
    }
    return res
 })
 
 async function refreshLogs() {
    loading.value = true
    try {
       if (systemStore.isSimulationMode) {
           // --- Mock Data Mode ---
           await new Promise(r => setTimeout(r, 600)) // Fake latency
           
           totalLogs.value = 500
           const mockPage = []
           const levels = ['INFO', 'WARN', 'RISK']
           for(let i=0; i<pageSize.value; i++) {
               const r = Math.random()
               let level = 'INFO'
               if (r > 0.9) level = 'RISK'
               else if (r > 0.7) level = 'WARN'
               
               mockPage.push({
                   id: `mock-trace-${Date.now()}-${i}`,
                   timestamp: dayjs().subtract(i * 10, 'minute').format('YYYY-MM-DD HH:mm:ss'),
                   level: level as any,
                   message: systemStore.settings.general.language === 'zh'
                       ? `模拟日志条目 #${i} 用于演示目的。分析结果：${level === 'RISK' ? '检测到恶意模式' : '常规操作'}。`
                       : `Simulated log entry #${i} for demo purposes. Analysis result: ${level === 'RISK' ? 'Malicious pattern detected' : 'Routine operation'}.`
               })
           }
           historyLogs.value = mockPage
           return
       }
 
       // Re-use system store's fetch capability but targeting history page
       const res = await fetch(`/api/history?page=${currentPage.value}&pageSize=${pageSize.value}`)
       if(res.ok) {
          const data = await res.json()
          totalLogs.value = data.total_count
          
          historyLogs.value = data.logs.map((item: any) => {
              let level = 'INFO'
              if(['Critical', 'High', 'RISK'].includes(item.risk_level)) level = 'RISK'
              else if(['Warning', 'Medium', 'WARN'].includes(item.risk_level)) level = 'WARN'
              
              return {
                  id: item.trace_id,
                  timestamp: item.processed_at,
                  level: level,
                  message: item.summary
              }
          })
       } else {
          throw new Error('API Error')
       }
    } catch(e) {
       console.error("Fetch failed", e)
       // Fallback to empty or error state, but don't auto-mock if simulation mode is OFF
       // unless we want to be very resilient. For now, empty is correct if backend is down and SimMode is off.
    } finally {
       loading.value = false
    }
 }
 
 function showDetails(row: LogEntry) {
    selectedLog.value = row
    dialogVisible.value = true
 }
 
 function getLevelBadgeClass(level: string) {
    switch(level) {
       case 'RISK': return 'bg-red-900/50 text-red-400 border border-red-500/30'
       case 'WARN': return 'bg-yellow-900/50 text-yellow-400 border border-yellow-500/30'
       default: return 'bg-gray-700 text-gray-300'
    }
 }
 
 function getLevelTextClass(level: string) {
     switch(level) {
       case 'RISK': return 'text-red-400 font-bold'
       case 'WARN': return 'text-yellow-400 font-bold'
       default: return 'text-gray-300'
    }
 }
 
 const tableRowClassName = ({ row }: { row: LogEntry }) => {
   if (row.level === 'RISK') {
     return 'warning-row'
   }
   return ''
 }
 
 onMounted(() => {
    refreshLogs()
 })
 </script>
 
 <style scoped>
 :deep(.el-table) {
    --el-table-bg-color: transparent;
    --el-table-tr-bg-color: transparent;
    --el-table-header-bg-color: rgba(0,0,0,0.2);
    --el-table-text-color: #d1d5db;
    --el-table-header-text-color: #9ca3af;
    --el-table-border-color: #374151;
 }
 
 :deep(.el-table__row:hover) {
    background-color: rgba(255,255,255,0.05) !important;
 }
 
 :deep(.warning-row) {
   background-color: rgba(127, 29, 29, 0.1) !important;
 }
 </style>
 