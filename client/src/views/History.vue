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
           <el-option label="Safe" value="Safe" />
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
             @keydown.enter="handleSearch"
          />
       </div>
 
       <el-button type="primary" :icon="Search" size="small" @click="handleSearch" :loading="loading">
          {{ $t('history.search') }}
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
 import { Search } from '@element-plus/icons-vue'
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
 
 // Direct binding to table, as filtering is now server-side
 const filteredLogs = computed(() => historyLogs.value)

 function handleSearch() {
    currentPage.value = 1
    refreshLogs()
 }
 
 async function refreshLogs() {
    loading.value = true
    try {
       if (systemStore.isSimulationMode) {
           // --- Mock Data Mode ---
           await new Promise(r => setTimeout(r, 600)) // Fake latency
           
           totalLogs.value = 500
           const mockPage = []
           for(let i=0; i<pageSize.value; i++) {
               const r = Math.random()
               let level = 'info'
               if (r > 0.95) level = 'critical'
               else if (r > 0.9) level = 'error'
               else if (r > 0.8) level = 'warning'
               else if (r > 0.6) level = 'safe'
               
               mockPage.push({
                   id: `mock-trace-${Date.now()}-${i}`,
                   timestamp: dayjs().subtract(i * 10, 'minute').format('YYYY-MM-DD HH:mm:ss'),
                   level: (level.charAt(0).toUpperCase() + level.slice(1)) as any, // UI expects Title Case for badges currently
                   message: systemStore.settings.general.language === 'zh'
                       ? `模拟日志条目 #${i} 用于演示目的。分析结果：${level === 'critical' ? '检测到严重风险' : '常规操作'}。`
                       : `Simulated log entry #${i} for demo purposes. Analysis result: ${level === 'critical' ? 'Critical risk detected' : 'Routine operation'}.`
               })
           }
           historyLogs.value = mockPage
           return
       }
 
       // Server-side filtering
       const params = new URLSearchParams({
          page: String(currentPage.value),
          pageSize: String(pageSize.value)
       })
       if(filterLevel.value) params.append('level', filterLevel.value)
       if(searchQuery.value) params.append('search', searchQuery.value)

       const res = await fetch(`/api/history?${params.toString()}`)
       if(res.ok) {
          const data = await res.json()
          totalLogs.value = data.total_count
          
          historyLogs.value = data.logs.map((item: any) => {
              let level = item.risk_level;
              // Normalize backend values to UI Display values
              const lower = level.toLowerCase();
              if (lower === 'critical' || lower === 'high') level = 'Critical';
              else if (lower === 'error' || lower === 'medium') level = 'Error';
              else if (lower === 'warning' || lower === 'low') level = 'Warning';
              else if (lower === 'info') level = 'Info';
              else if (lower === 'safe') level = 'Safe';
              // else keep original or Capitalize
              
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
       case 'Critical': return 'bg-red-900/50 text-red-400 border border-red-500/30'
       case 'Error': return 'bg-orange-900/50 text-orange-400 border border-orange-500/30'
       case 'Warning': return 'bg-yellow-900/50 text-yellow-400 border border-yellow-500/30'
       case 'Info': return 'bg-gray-700 text-gray-300 border border-gray-600/30'
       case 'Safe': return 'bg-green-900/50 text-green-400 border border-green-500/30'
       default: return 'bg-gray-700 text-gray-300'
    }
 }
 
 function getLevelTextClass(level: string) {
     switch(level) {
       case 'Critical': return 'text-red-400 font-bold'
       case 'Error': return 'text-orange-400 font-bold'
       case 'Warning': return 'text-yellow-400 font-bold'
       case 'Info': return 'text-gray-400 font-bold'
       case 'Safe': return 'text-green-400 font-bold'
       default: return 'text-gray-300'
    }
 }
 
 const tableRowClassName = ({ row }: { row: LogEntry }) => {
   if (row.level === 'Critical' || row.level === 'Error') {
     return 'warning-row'
   }
   return ''
 }

 // Watchers for filter interactivity
 // watch([filterLevel, searchQuery], () => {
 //    currentPage.value = 1
 //    refreshLogs()
 // })
 
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
 