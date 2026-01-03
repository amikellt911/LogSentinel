<template>
  <div class="h-full flex flex-col overflow-hidden">
    <!-- 页面标题 -->
    <div class="px-6 pt-6 pb-2">
      <h1 class="text-2xl font-bold text-gray-100 flex items-center gap-3">
        <el-icon class="text-blue-400"><Monitor /></el-icon>
        {{ $t('layout.serviceMonitor') }}
      </h1>
      <p class="text-gray-500 text-sm mt-1">
        {{ $t('serviceMonitor.subtitle') }}
      </p>
    </div>

    <!-- 主体内容区 -->
    <div class="flex-1 overflow-y-auto custom-scrollbar px-6 pb-6 space-y-6">
      <!-- 顶部：业务健康卡片（3个卡片） -->
      <BusinessHealthCards />

      <!-- 中部：风险分布 + 最近告警（左右布局） -->
      <div class="flex flex-col lg:flex-row gap-4">
        <!-- 左侧：风险分布饼图 -->
        <div class="lg:w-[30%]">
          <RiskDistribution />
        </div>

        <!-- 右侧：最近告警列表 -->
        <div class="lg:w-[70%]">
          <RecentAlerts />
        </div>
      </div>

      <!-- 底部：AI 态势感知流 -->
      <div class="min-h-[400px]">
        <AISituationStream />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted } from 'vue'
import BusinessHealthCards from '../components/BusinessHealthCards.vue'
import RiskDistribution from '../components/RiskDistribution.vue'
import RecentAlerts from '../components/RecentAlerts.vue'
import AISituationStream from '../components/AISituationStream.vue'
import { Monitor } from '@element-plus/icons-vue'

// TODO: 添加页面加载动画（分块加载，首屏优先）
// TODO: 替换所有 Mock 数据为真实 API 调用

onMounted(() => {
  // 页面挂载后的初始化逻辑
  console.log('ServiceMonitor 页面已加载')
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

.custom-scrollbar::-webkit-scrollbar-track {
  background: transparent;
}
</style>
