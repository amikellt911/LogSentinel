<template>
  <div class="bg-gray-800 rounded-lg shadow-lg border border-gray-700 flex flex-col h-full overflow-hidden">
    <!-- 工具栏 -->
    <div class="p-4 border-b border-gray-700 flex justify-between items-center bg-gray-900/50">
      <h3 class="text-gray-300 font-bold flex items-center gap-2">
        <el-icon><List /></el-icon> {{ $t('aiEngine.archiveList.title') }}
      </h3>
      <div class="flex gap-2">
         <el-input
            v-model="searchQuery"
            :placeholder="$t('aiEngine.archiveList.searchPlaceholder')"
            size="small"
            style="width: 200px"
            clearable
         >
           <template #prefix><el-icon><Search /></el-icon></template>
         </el-input>
      </div>
    </div>

    <!-- 表格 -->
    <div class="flex-1 overflow-auto custom-scrollbar">
      <el-table
        :data="filteredData"
        style="width: 100%; background: transparent;"
        :row-style="{ background: 'transparent' }"
        :header-cell-style="{ background: '#111827', color: '#9CA3AF', borderBottom: '1px solid #374151' }"
        row-key="id"
        @expand-change="handleExpand"
      >
        <!-- 展开列 (显示详情) -->
        <el-table-column type="expand">
            <template #default="props">
                <div class="p-4 bg-[#111827] border-t border-b border-gray-700">
                    <!-- 展开的卡片内容 (复用 BatchInsights 设计) -->
                    <div class="flex gap-6">
                         <!-- 左侧: 状态图标 -->
                         <div class="flex flex-col items-center w-24 shrink-0 pt-2 border-r border-gray-700/50 pr-4">
                             <div class="w-12 h-12 rounded-full border-4 flex items-center justify-center bg-gray-900 relative z-10"
                                  :class="getStatusBorderColor(props.row.riskLevel)">
                                 <el-icon :size="20" :class="getStatusTextColor(props.row.riskLevel)">
                                     <component :is="getIcon(props.row.riskLevel)" />
                                 </el-icon>
                             </div>
                             <span class="text-[10px] uppercase font-bold tracking-widest mt-2" :class="getStatusTextColor(props.row.riskLevel)">
                                 {{ props.row.riskLevel }}
                             </span>
                         </div>

                         <!-- 右侧: 详细信息 -->
                         <div class="flex-1">
                             <div class="flex justify-between items-start mb-2">
                                <h4 class="text-sm font-bold text-gray-200">AI Analysis Summary</h4>
                                <div class="flex gap-2">
                                    <span v-for="tag in props.row.tags" :key="tag"
                                          class="text-[10px] px-2 py-0.5 rounded-full border border-gray-600 text-gray-400 bg-gray-800">
                                        #{{ tag }}
                                    </span>
                                </div>
                             </div>
                             <p class="text-gray-400 text-sm leading-relaxed font-mono mb-4">{{ props.row.summary }}</p>

                             <div class="flex gap-8 text-xs font-mono text-gray-500 border-t border-gray-800 pt-3 mt-auto">
                                 <div>Latency: <span class="text-blue-400">{{ props.row.avgLatency }}ms</span></div>
                                 <div>Tokens: <span class="text-purple-400">{{ props.row.tokenCount }}</span></div>
                                 <div>Created: {{ props.row.createdAt }}</div>
                             </div>
                         </div>
                    </div>
                </div>
            </template>
        </el-table-column>

        <el-table-column prop="id" label="Batch ID" width="120">
            <template #default="scope">
                <span class="font-mono text-blue-400">#{{ scope.row.id }}</span>
            </template>
        </el-table-column>

        <el-table-column prop="createdAt" :label="$t('aiEngine.archiveList.time')" width="120" />

        <el-table-column prop="logCount" :label="$t('aiEngine.archiveList.logs')" width="100" align="center">
            <template #default="scope">
                <el-tag type="info" size="small" effect="dark">{{ scope.row.logCount }}</el-tag>
            </template>
        </el-table-column>

        <el-table-column prop="riskLevel" :label="$t('aiEngine.archiveList.risk')" width="120">
             <template #default="scope">
                <el-tag :type="getLevelTagType(scope.row.riskLevel)" size="small" effect="dark">{{ scope.row.riskLevel }}</el-tag>
             </template>
        </el-table-column>

        <el-table-column prop="tokenCount" :label="$t('aiEngine.archiveList.tokens')" width="100" align="right">
             <template #default="scope">
                <span class="font-mono text-gray-300">{{ scope.row.tokenCount }}</span>
             </template>
        </el-table-column>

        <el-table-column :label="$t('aiEngine.archiveList.actions')" width="150" align="right">
            <template #default="scope">
                <el-button link type="primary" size="small" @click.stop="openDebug(scope.row)">
                    <el-icon class="mr-1"><Monitor /></el-icon> {{ $t('aiEngine.archiveList.debug') }}
                </el-button>
            </template>
        </el-table-column>
      </el-table>
    </div>

    <!-- 分页 (Mock) -->
    <div class="p-4 border-t border-gray-700 flex justify-end bg-gray-900/50">
       <el-pagination background layout="prev, pager, next" :total="100" />
    </div>

  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { Search, List, Monitor, Warning, CircleCheck, CircleClose, InfoFilled } from '@element-plus/icons-vue'
import type { BatchArchiveItem } from '../types/aiEngine'

const props = defineProps<{
    data: BatchArchiveItem[]
}>()

const emit = defineEmits(['debug'])

const searchQuery = ref('')

// 过滤列表数据
const filteredData = computed(() => {
    if(!searchQuery.value) return props.data
    const q = searchQuery.value.toLowerCase()
    return props.data.filter(item =>
        item.id.toString().includes(q) ||
        item.summary.toLowerCase().includes(q)
    )
})

// 打开调试器
const openDebug = (row: BatchArchiveItem) => {
    emit('debug', row)
}

const handleExpand = () => {
    // 如果需要只允许展开一行，可以在这里实现
}

// --- 辅助函数 ---
function getLevelTagType(level: string) {
    switch(level) {
        case 'CRITICAL': return 'danger'
        case 'ERROR': return 'danger'
        case 'WARNING': return 'warning'
        case 'INFO': return 'info'
        case 'SAFE': return 'success'
        default: return 'info'
    }
}

function getStatusBorderColor(status: string) {
    switch(status) {
        case 'CRITICAL': return 'border-red-500 bg-red-900/20'
        case 'ERROR': return 'border-red-500 bg-red-900/20'
        case 'WARNING': return 'border-yellow-500 bg-yellow-900/20'
        case 'SAFE': return 'border-green-500 bg-green-900/20'
        default: return 'border-blue-500 bg-blue-900/20'
    }
}

function getStatusTextColor(status: string) {
    switch(status) {
        case 'CRITICAL': return 'text-red-500'
        case 'ERROR': return 'text-red-500'
        case 'WARNING': return 'text-yellow-500'
        case 'SAFE': return 'text-green-500'
        default: return 'text-blue-500'
    }
}

function getIcon(status: string) {
    switch(status) {
        case 'CRITICAL': return CircleClose
        case 'ERROR': return CircleClose
        case 'WARNING': return Warning
        case 'SAFE': return CircleCheck
        default: return InfoFilled
    }
}
</script>

<style scoped>
.custom-scrollbar::-webkit-scrollbar {
  width: 6px;
  height: 6px;
}
.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #374151;
  border-radius: 3px;
}

:deep(.el-table) {
  --el-table-border-color: #374151;
  --el-table-header-bg-color: #1f2937;
  --el-table-tr-bg-color: transparent;
  color: #d1d5db;
}
:deep(.el-table__row:hover) {
  background-color: rgba(55, 65, 81, 0.5) !important;
}
:deep(.el-table__expanded-cell) {
  padding: 0 !important;
  background-color: transparent !important;
}
</style>
