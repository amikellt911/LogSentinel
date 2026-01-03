<template>
  <div class="bg-gray-800/40 border border-gray-700 rounded-lg overflow-hidden">
    <!-- 表格 -->
    <el-table
      ref="tableRef"
      :data="tableData"
      style="width: 100%"
      :header-cell-style="{ background: '#1f2937', color: '#9ca3af' }"
      :cell-style="{ background: 'transparent', color: '#d1d5db' }"
      @expand-change="handleExpandChange"
    >
      <!-- 展开/折叠列 -->
      <el-table-column type="expand">
        <template #default="{ row }">
          <!-- 详情卡片（复用 BatchInsights 的样式） -->
          <div class="p-4 m-2 bg-gray-900/50 border border-gray-700 rounded-lg">
            <div class="flex gap-6">
              <!-- 左侧：时间 + 状态节点 -->
              <div class="flex flex-col items-end w-32 shrink-0 pt-1">
                <span class="font-mono text-gray-500 text-xs">{{ row.time }}</span>
                <div class="mt-2 w-10 h-10 rounded-full border-4 flex items-center justify-center bg-gray-900 relative z-10 transition-colors duration-500"
                     :class="getStatusBorderColor(row.risk_level)">
                  <el-icon :size="16" :class="getStatusTextColor(row.risk_level)">
                    <component :is="getStatusIcon(row.risk_level)" />
                  </el-icon>
                </div>
                <span class="text-[10px] uppercase font-bold tracking-widest mt-1" :class="getStatusTextColor(row.risk_level)">
                  {{ row.risk_level }}
                </span>
              </div>

              <!-- 右侧：卡片内容 -->
              <div class="flex-1 bg-gray-800/60 border border-gray-700 rounded-lg p-4 hover:bg-gray-800/80 transition-all duration-300">
                <!-- Header -->
                <div class="flex justify-between items-start mb-3 border-b border-gray-700/50 pb-2">
                  <div>
                    <h3 class="text-sm font-bold text-gray-200 flex items-center gap-2 flex-wrap">
                      <span>{{ row.service_name }}</span>
                      <span class="text-gray-500">|</span>
                      <span>{{ $t('aiEngine.trace') }} #{{ row.trace_id }}</span>
                      <span class="px-1.5 py-0.5 rounded bg-gray-700 text-[10px] text-gray-400 font-normal">
                        {{ $t('aiEngine.spanCount') }}: {{ row.log_count }}
                      </span>
                      <span class="px-1.5 py-0.5 rounded bg-gray-700 text-[10px] text-gray-400 font-normal">
                        Tokens: {{ row.token_count }}
                      </span>
                    </h3>
                  </div>
                  <div class="flex gap-2">
                    <span v-for="tag in row.tags" :key="tag"
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
                      <div class="text-xs text-gray-500 uppercase">{{ $t('insights.latency') }}</div>
                      <div class="text-sm font-mono text-blue-400">{{ row.avg_latency }}ms</div>
                    </div>
                  </div>

                  <!-- AI Summary -->
                  <div class="flex-1">
                    <div class="text-xs text-purple-400 mb-1 font-bold uppercase tracking-wider flex items-center gap-1">
                      <el-icon><MagicStick /></el-icon> {{ $t('insights.aiSummary') }}
                    </div>
                    <p class="text-gray-300 text-sm leading-relaxed font-mono">
                      {{ row.summary }}
                    </p>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </template>
      </el-table-column>

      <!-- Trace ID -->
      <el-table-column prop="trace_id" :label="$t('aiEngine.traceId')" min-width="180">
        <template #default="{ row }">
          <span class="font-mono text-blue-400">{{ row.trace_id }}</span>
        </template>
      </el-table-column>

      <!-- 创建时间 -->
      <el-table-column prop="created_at" :label="$t('aiEngine.createdAt')" min-width="160">
        <template #default="{ row }">
          <span class="font-mono text-gray-400">{{ row.created_at }}</span>
        </template>
      </el-table-column>

      <!-- 日志数量 -->
      <el-table-column prop="log_count" :label="$t('aiEngine.logCount')" min-width="100" align="center">
        <template #default="{ row }">
          <span class="font-mono text-gray-300">{{ row.log_count }}</span>
        </template>
      </el-table-column>

      <!-- 风险等级 -->
      <el-table-column prop="risk_level" :label="$t('aiEngine.riskLevel')" min-width="100" align="center">
        <template #default="{ row }">
          <el-tag :type="getRiskTagType(row.risk_level)" effect="dark" size="small">
            {{ row.risk_level }}
          </el-tag>
        </template>
      </el-table-column>

      <!-- Token 消耗 -->
      <el-table-column prop="token_count" :label="$t('aiEngine.tokenCount')" min-width="100" align="center">
        <template #default="{ row }">
          <span class="font-mono text-purple-400">{{ row.token_count }}</span>
        </template>
      </el-table-column>

      <!-- 操作 -->
      <el-table-column :label="$t('common.operations')" min-width="180" fixed="right">
        <template #default="{ row }">
          <el-button
            type="primary"
            size="small"
            @click="toggleExpand(row)"
            link
          >
            <el-icon class="mr-1"><View /></el-icon>
            {{ expandedRows.has(row.trace_id) ? $t('aiEngine.collapse') : $t('aiEngine.viewDetails') }}
          </el-button>
          <el-button
            type="warning"
            size="small"
            @click="openPromptDebugger(row)"
            link
          >
            <el-icon class="mr-1"><Document /></el-icon>
            {{ $t('aiEngine.promptDebugger') }}
          </el-button>
        </template>
      </el-table-column>
    </el-table>

    <!-- 分页 -->
    <div class="flex justify-between items-center p-4 border-t border-gray-700">
      <div class="text-sm text-gray-500">
        {{ $t('aiEngine.total') }}: {{ pagination.total }} {{ $t('aiEngine.batches') }}
      </div>
      <el-pagination
        v-model:current-page="pagination.currentPage"
        v-model:page-size="pagination.pageSize"
        :page-sizes="[10, 20, 50, 100]"
        :total="pagination.total"
        layout="sizes, prev, pager, next, jumper"
        @current-change="handlePageChange"
        @size-change="handleSizeChange"
        background
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { View, Document, MagicStick, Warning, CircleCheck, CircleClose } from '@element-plus/icons-vue'
import dayjs from 'dayjs'

// TODO: Replace with real API
/**
 * 批次存档列表项数据结构
 */
interface BatchArchiveItem {
  trace_id: string
  service_name: string              // 服务名称
  created_at: string
  time: string                      // 时分秒（用于卡片显示）
  log_count: number                 // Span 数量
  risk_level: 'CRITICAL' | 'ERROR' | 'WARNING' | 'INFO' | 'SAFE'
  token_count: number
  avg_latency: number
  summary: string
  tags: string[]
}

const emit = defineEmits<{
  openPromptDebugger: [batch: BatchArchiveItem]
}>()

// 表格数据
const tableData = ref<BatchArchiveItem[]>([])
const tableRef = ref()
const expandedRows = ref<Set<string>>(new Set())

// 分页
const pagination = reactive({
  currentPage: 1,
  pageSize: 10,
  total: 0
})

/**
 * Mock 数据生成器
 */
function generateMockData() {
  const risks: Array<'CRITICAL' | 'ERROR' | 'WARNING' | 'INFO' | 'SAFE'> = ['CRITICAL', 'ERROR', 'WARNING', 'INFO', 'SAFE']
  const services = ['auth-service', 'api-gateway', 'payment-service', 'user-service', 'order-service']
  const templates = [
    { summary: '流量模式正常。用户认证流程运行符合预期。', tags: ['认证', '正常'], risk_level: 'SAFE' as const },
    { summary: '检测到 SQL 注入尝试，攻击载荷已拦截。', tags: ['安全', '注入'], risk_level: 'CRITICAL' as const },
    { summary: '数据库查询延迟略有升高，建议监控。', tags: ['数据库', '性能'], risk_level: 'WARNING' as const },
    { summary: '批处理完成，系统运行平稳。', tags: ['正常'], risk_level: 'INFO' as const },
    { summary: '检测到异常登录行为，已触发告警。', tags: ['安全', '登录'], risk_level: 'ERROR' as const }
  ]

  const data: BatchArchiveItem[] = []
  for (let i = 0; i < 25; i++) {
    const tpl = templates[Math.floor(Math.random() * templates.length)]
    const service = services[Math.floor(Math.random() * services.length)]
    const date = dayjs().subtract(i * 5, 'minute')
    data.push({
      trace_id: `trace-${Math.random().toString(36).substring(2, 15)}`,
      service_name: service,
      created_at: date.format('YYYY-MM-DD HH:mm:ss'),
      time: date.format('HH:mm:ss'),
      log_count: Math.floor(Math.random() * 40) + 10,
      risk_level: tpl.risk_level,
      token_count: Math.floor(Math.random() * 500) + 200,
      avg_latency: Math.floor(Math.random() * 50) + 20,
      summary: tpl.summary,
      tags: tpl.tags
    })
  }

  return data
}

/**
 * 切换展开/折叠状态
 */
function toggleExpand(row: BatchArchiveItem) {
  tableRef.value.toggleRowExpansion(row)
}

/**
 * 表格展开变化事件
 */
function handleExpandChange(row: BatchArchiveItem, expandedRowsList: BatchArchiveItem[]) {
  const isExpanded = expandedRowsList.some(r => r.trace_id === row.trace_id)
  if (isExpanded) {
    expandedRows.value.add(row.trace_id)
  } else {
    expandedRows.value.delete(row.trace_id)
  }
}

/**
 * 打开 Prompt 透视抽屉
 */
function openPromptDebugger(batch: BatchArchiveItem) {
  emit('openPromptDebugger', batch)
}

/**
 * 获取风险等级对应的 Tag 类型
 */
function getRiskTagType(riskLevel: string): 'danger' | 'warning' | 'info' | 'success' {
  switch (riskLevel) {
    case 'CRITICAL':
    case 'ERROR':
      return 'danger'
    case 'WARNING':
      return 'warning'
    case 'INFO':
      return 'info'
    case 'SAFE':
      return 'success'
    default:
      return 'info'
  }
}

/**
 * 获取状态边框颜色
 */
function getStatusBorderColor(status: string) {
  switch (status) {
    case 'CRITICAL':
    case 'ERROR':
      return 'border-red-500 bg-red-900/20'
    case 'WARNING':
      return 'border-yellow-500 bg-yellow-900/20'
    case 'INFO':
      return 'border-blue-500 bg-blue-900/20'
    case 'SAFE':
    default:
      return 'border-green-500 bg-green-900/20'
  }
}

/**
 * 获取状态文本颜色
 */
function getStatusTextColor(status: string) {
  switch (status) {
    case 'CRITICAL':
    case 'ERROR':
      return 'text-red-500'
    case 'WARNING':
      return 'text-yellow-500'
    case 'INFO':
      return 'text-blue-500'
    case 'SAFE':
    default:
      return 'text-green-500'
  }
}

/**
 * 获取状态图标
 */
function getStatusIcon(status: string) {
  switch (status) {
    case 'CRITICAL':
    case 'ERROR':
      return CircleClose
    case 'WARNING':
      return Warning
    case 'INFO':
    case 'SAFE':
    default:
      return CircleCheck
  }
}

/**
 * 分页变化
 */
function handlePageChange(page: number) {
  pagination.currentPage = page
  // TODO: Reload data with new page
}

function handleSizeChange(size: number) {
  pagination.pageSize = size
  pagination.currentPage = 1
  // TODO: Reload data with new page size
}

onMounted(() => {
  const data = generateMockData()
  tableData.value = data
  pagination.total = data.length
})
</script>

<style scoped>
/* Element Plus 表格样式覆盖 */
:deep(.el-table) {
  background-color: transparent;
  color: #d1d5db;
}

:deep(.el-table tr) {
  background-color: transparent;
}

:deep(.el-table--enable-row-hover .el-table__body tr:hover > td) {
  background-color: rgba(55, 65, 81, 0.3) !important;
}

:deep(.el-table td.el-table__cell) {
  border-color: #374151;
}

:deep(.el-pagination.is-background .el-pager li) {
  background-color: #374151;
  color: #d1d5db;
}

:deep(.el-pagination.is-background .el-pager li.is-active) {
  background-color: #3b82f6;
  color: white;
}

:deep(.el-pagination.is-background button) {
  background-color: #374151;
  color: #d1d5db;
}

:deep(.el-select .el-input__wrapper) {
  background-color: #374151;
  box-shadow: 0 0 0 1px #4b5563 inset;
}

:deep(.el-select .el-input__inner) {
  color: #d1d5db;
}
</style>
