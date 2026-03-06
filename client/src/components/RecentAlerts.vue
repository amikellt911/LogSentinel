<template>
  <div class="bg-gray-800/30 border border-gray-700 rounded p-4 flex flex-col min-h-[300px]">
    <h3 class="text-xs font-bold text-gray-400 uppercase tracking-widest mb-4">
      {{ $t('dashboard.recentAlerts') }}
    </h3>
    <div class="flex-1 overflow-hidden">
      <el-table
        :data="alerts"
        style="width: 100%; --el-table-bg-color: transparent; --el-table-tr-bg-color: transparent; --el-table-header-bg-color: transparent; --el-table-border-color: #374151; --el-table-text-color: #9ca3af; --el-table-header-text-color: #d1d5db;"
        size="small"
        :row-style="{ background: 'transparent' }"
        :header-row-style="{ background: 'transparent' }"
      >
        <!-- 时间列 -->
        <el-table-column prop="time" :label="$t('dashboard.table.time')" width="100" />

        <!-- 服务列 -->
        <el-table-column
          prop="service"
          :label="$t('dashboard.table.service')"
          width="140"
        />

        <!-- 级别列（带颜色标识） -->
        <el-table-column prop="level" :label="$t('dashboard.table.level')" width="100">
          <template #default="{ row }">
            <span class="px-2 py-0.5 rounded text-xs font-bold" :class="getLevelClass(row.level)">
              {{ row.level }}
            </span>
          </template>
        </el-table-column>

        <!-- 摘要列 -->
        <el-table-column prop="summary" :label="$t('dashboard.table.summary')" />

        <!-- 操作列 -->
        <el-table-column :label="$t('history.table.actions')" width="100" align="center">
          <template #default="{ row }">
            <el-button
              type="primary"
              link
              size="small"
              @click="viewDetails(row)"
            >
              {{ $t('history.details') }}
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { useSystemStore } from '../stores/system'
import { ElMessage } from 'element-plus'

const systemStore = useSystemStore()

// TODO: Replace with real API
// TODO: 替换为真实 API 数据
const alerts = ref(systemStore.recentAlerts)

/**
 * 根据风险等级返回对应的样式类名
 * @param level - 风险等级（Critical/Error/Warning/Info/Safe）
 * @returns Tailwind CSS 样式类名
 */
function getLevelClass(level: string): string {
  switch (level) {
    case 'Critical':
      return 'bg-red-900/50 text-red-400'
    case 'Error':
      return 'bg-orange-900/50 text-orange-400'
    case 'Warning':
      return 'bg-yellow-900/50 text-yellow-400'
    case 'Info':
      return 'bg-blue-900/50 text-blue-400'
    case 'Safe':
      return 'bg-green-900/50 text-green-400'
    default:
      return 'bg-gray-700 text-gray-300'
  }
}

/**
 * 查看告警详情
 * TODO: 后续可以打开弹窗或抽屉显示详细信息
 * @param row - 告警数据行
 */
function viewDetails(row: any) {
  // TODO: 实现详情查看功能（打开弹窗或抽屉）
  ElMessage.info(`查看详情: ${row.summary}`)
}
</script>

<style scoped>
/* 表格深色主题样式覆盖 */
:deep(.el-table th.el-table__cell) {
  background-color: rgba(30, 30, 30, 0.5) !important;
}

:deep(.el-table tr) {
  background-color: transparent !important;
}

:deep(.el-table td.el-table__cell),
:deep(.el-table th.el-table__cell.is-leaf) {
  border-bottom: 1px solid #374151 !important;
}

:deep(.el-table--enable-row-hover .el-table__body tr:hover > td.el-table__cell) {
  background-color: rgba(255, 255, 255, 0.05) !important;
}

:deep(.el-table__inner-wrapper::before) {
  display: none;
}
</style>
