<template>
  <div class="h-full flex flex-col bg-gray-800/30 rounded border border-gray-700 overflow-hidden">
    <!-- 表格 -->
    <div class="flex-1 overflow-hidden">
      <el-table
        :data="data"
        style="width: 100%; height: 100%;"
        v-loading="loading"
        element-loading-background="rgba(0, 0, 0, 0.7)"
        :row-class-name="tableRowClassName"
        @row-click="handleRowClick"
      >
        <!-- Trace ID（可点击，自适应） -->
        <el-table-column prop="trace_id" :label="$t('traceExplorer.table.traceId')" min-width="220">
          <template #default="{ row }">
            <span class="text-blue-400 font-mono text-sm hover:text-blue-300 cursor-pointer">
              {{ row.trace_id }}
            </span>
          </template>
        </el-table-column>

        <!-- 服务名称（自适应） -->
        <el-table-column prop="service_name" :label="$t('traceExplorer.table.serviceName')" min-width="160" />

        <!-- 开始时间（窄） -->
        <el-table-column prop="start_time" :label="$t('traceExplorer.table.startTime')" width="140" sortable />

        <!-- 耗时（窄） -->
        <el-table-column prop="duration" :label="$t('traceExplorer.table.duration')" width="85" sortable>
          <template #default="{ row }">
            <span class="font-mono text-sm" :class="getDurationClass(row.duration)">
              {{ row.duration }}ms
            </span>
          </template>
        </el-table-column>

        <!-- Span 数量 -->
        <el-table-column prop="span_count" :label="$t('traceExplorer.table.spanCount')" width="90" sortable>
          <template #default="{ row }">
            <span class="font-mono text-sm text-gray-400">{{ row.span_count }}</span>
          </template>
        </el-table-column>

        <!-- Token 消耗（新增） -->
        <el-table-column prop="token_count" :label="$t('traceExplorer.table.tokenCount')" width="100" sortable>
          <template #default="{ row }">
            <span class="font-mono text-sm text-purple-400">{{ formatNumber(row.token_count) }}</span>
          </template>
        </el-table-column>

        <!-- 风险等级（带颜色标识） -->
        <el-table-column prop="risk_level" :label="$t('traceExplorer.table.riskLevel')" width="110">
          <template #default="{ row }">
            <span class="px-2 py-1 rounded text-xs font-bold" :class="getLevelBadgeClass(row.risk_level)">
              {{ row.risk_level }}
            </span>
          </template>
        </el-table-column>

        <!-- 列表里单独拉一列 AI 状态，是为了把“风险等级未知”和“AI 没跑/失败”拆开。 -->
        <el-table-column prop="ai_status" label="AI 状态" width="120">
          <template #default="{ row }">
            <span class="px-2 py-1 rounded text-xs font-bold" :class="getAiStatusBadgeClass(row.ai_status)">
              {{ getAiStatusLabel(row.ai_status) }}
            </span>
          </template>
        </el-table-column>

        <!-- 操作按钮（统一为查看详情） -->
        <el-table-column :label="$t('traceExplorer.table.actions')" width="120" fixed="right">
          <template #default="{ row }">
            <el-button
              type="primary"
              :icon="View"
              size="small"
              @click.stop="handleViewDetail(row)"
            >
              {{ $t('traceExplorer.table.viewDetails') }}
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <!-- 分页组件 -->
    <div class="flex justify-between items-center px-4 py-3 border-t border-gray-700 bg-gray-900/30">
      <div class="text-xs text-gray-500">
        {{ $t('traceExplorer.pagination.total') }}: {{ total }}
      </div>
      <el-pagination
        :current-page="currentPage"
        :page-size="pageSize"
        :page-sizes="[20, 50, 100]"
        :total="total"
        layout="sizes, prev, pager, next"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
        small
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { View } from '@element-plus/icons-vue'
import type { TraceListItem } from '../types/trace'

// Props
interface Props {
  data: TraceListItem[]
  loading?: boolean
  total: number
  currentPage: number
  pageSize: number
}

withDefaults(defineProps<Props>(), {
  loading: false
})

// Emits
const emit = defineEmits<{
  'row-click': [row: TraceListItem]
  'view-detail': [row: TraceListItem]
  'page-change': [page: number]
  'size-change': [size: number]
}>()

/**
 * 格式化数字（千分位分隔符）
 */
function formatNumber(num: number): string {
  return new Intl.NumberFormat('en-US').format(num)
}

/**
 * 根据风险等级返回徽章样式类名
 */
function getLevelBadgeClass(level: string): string {
  switch (level) {
    case 'Critical':
      return 'bg-red-900/50 text-red-400 border border-red-500/30'
    case 'Error':
      return 'bg-orange-900/50 text-orange-400 border border-orange-500/30'
    case 'Warning':
      return 'bg-yellow-900/50 text-yellow-400 border border-yellow-500/30'
    case 'Info':
      return 'bg-blue-900/50 text-blue-400 border border-blue-500/30'
    case 'Safe':
      return 'bg-green-900/50 text-green-400 border border-green-500/30'
    default:
      return 'bg-gray-700 text-gray-300'
  }
}

function getAiStatusLabel(status: string): string {
  switch (status) {
    case 'completed':
      return '已完成'
    case 'skipped_manual':
      return '已关闭'
    case 'skipped_circuit':
      return '熔断跳过'
    case 'failed_primary':
      return '主路失败'
    case 'failed_both':
      return '双路失败'
    default:
      return '处理中'
  }
}

function getAiStatusBadgeClass(status: string): string {
  switch (status) {
    case 'completed':
      return 'bg-emerald-900/40 text-emerald-300 border border-emerald-500/30'
    case 'skipped_manual':
      return 'bg-slate-700/50 text-slate-200 border border-slate-500/30'
    case 'skipped_circuit':
      return 'bg-amber-900/40 text-amber-300 border border-amber-500/30'
    case 'failed_primary':
    case 'failed_both':
      return 'bg-rose-900/40 text-rose-300 border border-rose-500/30'
    default:
      return 'bg-blue-900/40 text-blue-300 border border-blue-500/30'
  }
}

/**
 * 根据耗时返回样式类名
 */
function getDurationClass(duration: number): string {
  if (duration > 1000) return 'text-red-400 font-bold' // 超过 1s 为红色
  if (duration > 500) return 'text-yellow-400 font-bold' // 超过 500ms 为黄色
  return 'text-gray-300' // 正常为灰色
}

/**
 * 行点击事件
 */
function handleRowClick(row: TraceListItem) {
  emit('row-click', row)
}

/**
 * 查看详情按钮点击事件
 */
function handleViewDetail(row: TraceListItem) {
  emit('view-detail', row)
}

/**
 * 页码变化
 */
function handleCurrentChange(page: number) {
  emit('page-change', page)
}

/**
 * 每页数量变化
 */
function handleSizeChange(size: number) {
  emit('size-change', size)
}

/**
 * 表格行样式（高风险行高亮）
 */
function tableRowClassName({ row }: { row: TraceListItem }) {
  if (row.risk_level === 'Critical' || row.risk_level === 'Error') {
    return 'warning-row'
  }
  return ''
}
</script>

<style scoped>
/* 表格深色主题样式 */
:deep(.el-table) {
  --el-table-bg-color: transparent;
  --el-table-tr-bg-color: transparent;
  --el-table-header-bg-color: rgba(0, 0, 0, 0.2);
  --el-table-text-color: #d1d5db;
  --el-table-header-text-color: #9ca3af;
  --el-table-border-color: #374151;
}

:deep(.el-table__row:hover) {
  background-color: rgba(255, 255, 255, 0.05) !important;
  cursor: pointer;
}

:deep(.warning-row) {
  background-color: rgba(127, 29, 29, 0.1) !important;
}

/* 分页组件样式 */
:deep(.el-pagination) {
  --el-pagination-button-disabled-bg-color: rgba(30, 30, 30, 0.5);
  --el-pagination-bg-color: rgba(30, 30, 30, 0.5);
  --el-pagination-text-color: #d1d5db;
  --el-pagination-button-bg-color: rgba(30, 30, 30, 0.5);
  --el-pagination-button-color: #d1d5db;
}

:deep(.el-pagination button:hover) {
  background-color: rgba(64, 158, 255, 0.1) !important;
}

:deep(.el-pager li.is-active) {
  background-color: rgba(64, 158, 255, 0.2) !important;
  color: #409eff !important;
}
</style>
