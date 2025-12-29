<template>
  <el-container class="h-screen w-screen overflow-hidden bg-bg-dark text-gray-200 font-sans">
    <!-- 侧边栏 -->
    <el-aside width="240px" class="border-r border-gray-700 flex flex-col bg-[#181818]">
      <div class="h-16 flex items-center justify-center border-b border-gray-700">
        <h1 class="text-xl font-bold tracking-wider text-primary">LOG<span class="text-white">SENTINEL</span></h1>
      </div>
      
      <el-menu
        :default-active="route.path === '/' ? '/' : route.path"
        class="flex-1 bg-transparent border-r-0 pt-4"
        text-color="#9ca3af"
        active-text-color="#409eff"
        background-color="transparent"
        router
      >
        <el-menu-item index="/">
          <el-icon><Monitor /></el-icon>
          <span>{{ $t('layout.serviceMonitor') }}</span>
        </el-menu-item>

        <el-menu-item index="/system">
          <el-icon><Odometer /></el-icon>
          <span>{{ $t('layout.systemStatus') }}</span>
        </el-menu-item>

        <el-menu-item index="/logs">
          <el-icon><Monitor /></el-icon>
          <span>{{ $t('layout.logs') }}</span>
        </el-menu-item>

        <el-menu-item index="/traces">
          <el-icon><Clock /></el-icon>
          <span>{{ $t('layout.traceExplorer') }}</span>
        </el-menu-item>

        <el-menu-item index="/ai-engine">
          <el-icon><DataAnalysis /></el-icon>
          <span>{{ $t('layout.aiEngine') }}</span>
        </el-menu-item>

        <el-menu-item index="/benchmark">
          <el-icon><Lightning /></el-icon>
          <span>{{ $t('layout.benchmark') }}</span>
        </el-menu-item>

        <el-menu-item index="/settings">
          <el-icon><Setting /></el-icon>
          <span>{{ $t('layout.settings') }}</span>
        </el-menu-item>
      </el-menu>

      <div class="p-4 border-t border-gray-700 text-xs text-gray-500 text-center">
        v1.0.0-build.2024
      </div>
    </el-aside>

    <el-container>
      <!-- 头部 -->
      <el-header height="64px" class="border-b border-gray-700 bg-bg-dark flex items-center justify-between px-6">
        <div class="text-lg font-medium text-gray-300">
          {{ currentRouteName }}
        </div>
        
        <div class="flex items-center gap-4">
          <!-- 模拟模式开关 -->
          <div class="flex items-center gap-2 border-r border-gray-700 pr-4 mr-2">
            <span class="text-xs font-mono uppercase tracking-widest text-gray-500">
              {{ $t('layout.simMode') }}
            </span>
            <el-switch
              v-model="systemStore.isSimulationMode"
              size="small"
              inline-prompt
              active-text="ON"
              inactive-text="OFF"
              style="--el-switch-on-color: #e6a23c;" 
            />
          </div>

          <span 
            class="text-xs font-mono uppercase tracking-widest transition-colors duration-300"
            :class="systemStore.isRunning ? 'text-success' : 'text-gray-500'"
          >
            {{ systemStore.isRunning ? $t('layout.systemRunning') : $t('layout.systemIdle') }}
          </span>
          
          <!-- 系统运行总开关 -->
          <el-switch
            v-model="systemStore.isRunning"
            @change="handleToggle"
            style="--el-switch-on-color: #13ce66; --el-switch-off-color: #ff4949"
            size="large"
            inline-prompt
            active-text="ON"
            inactive-text="OFF"
          />
        </div>
      </el-header>

      <!-- 主内容区 -->
      <el-main class="bg-bg-darker p-0 overflow-hidden relative">
        <router-view v-slot="{ Component }">
          <transition name="fade" mode="out-in">
            <component :is="Component" />
          </transition>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useSystemStore } from '../stores/system'
import { Odometer, Monitor, Setting, Clock, DataAnalysis, Lightning } from '@element-plus/icons-vue'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'

const route = useRoute()
const systemStore = useSystemStore()
const { t } = useI18n()

// 根据当前路由显示标题
const currentRouteName = computed(() => {
  switch (route.name) {
    case 'service': return t('layout.serviceMonitor')
    case 'system': return t('layout.systemStatus')
    case 'logs': return t('layout.eventStream')
    case 'ai-engine': return t('layout.aiEngine')
    case 'traces': return t('layout.traceExplorer')
    case 'benchmark': return t('layout.benchmark')
    case 'settings': return t('layout.configuration')
    default: return t('layout.dashboard')
  }
})

function handleToggle(val: string | number | boolean) {
  // 验证逻辑：如果未配置 API Key 且不是本地 Mock 模式，阻止开启
  if (val === true) {
    if (systemStore.settings.ai.provider !== 'Local-Mock' && !systemStore.settings.ai.apiKey) {
      ElMessage.error(t('settings.ai.apiKeyPlaceholder'))
      // 异步重置开关状态，避免闪烁
      setTimeout(() => {
        systemStore.toggleSystem(false)
      }, 0)
      return
    }
  }
  
  systemStore.toggleSystem(val as boolean)
}
</script>

<style scoped>
/* Element Menu 深色主题覆盖 */
:deep(.el-menu-item) {
  margin: 4px 16px;
  border-radius: 8px;
  height: 48px;
}

:deep(.el-menu-item:hover) {
  background-color: rgba(255, 255, 255, 0.05) !important;
}

:deep(.el-menu-item.is-active) {
  background-color: rgba(64, 158, 255, 0.15) !important;
}

/* 页面切换过渡动画 */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
