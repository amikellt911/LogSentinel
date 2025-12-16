<template>
  <el-container class="h-screen w-screen overflow-hidden bg-bg-dark text-gray-200 font-sans">
    <!-- Sidebar -->
    <el-aside width="240px" class="border-r border-gray-700 flex flex-col bg-[#181818]">
      <div class="h-16 flex items-center justify-center border-b border-gray-700">
        <h1 class="text-xl font-bold tracking-wider text-primary">LOG<span class="text-white">SENTINEL</span></h1>
      </div>
      
      <el-menu
        :default-active="route.path"
        class="flex-1 bg-transparent border-r-0 pt-4"
        text-color="#9ca3af"
        active-text-color="#409eff"
        background-color="transparent"
        router
      >
        <el-menu-item index="/">
          <el-icon><Odometer /></el-icon>
          <span>{{ $t('layout.dashboard') }}</span>
        </el-menu-item>

        <el-menu-item index="/logs">
          <el-icon><Monitor /></el-icon>
          <span>{{ $t('layout.logs') }}</span>
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
      <!-- Header -->
      <el-header height="64px" class="border-b border-gray-700 bg-bg-dark flex items-center justify-between px-6">
        <div class="text-lg font-medium text-gray-300">
          {{ currentRouteName }}
        </div>
        
        <div class="flex items-center gap-4">
           <!-- Language Switcher -->
           <button 
            @click="toggleLanguage"
            class="px-2 py-1 text-xs font-mono border border-gray-600 rounded text-gray-400 hover:text-white hover:border-gray-400 transition-colors"
          >
            {{ locale === 'en' ? 'EN' : '中文' }}
          </button>

          <!-- Simulation Mode Toggle -->
          <div class="flex items-center gap-2 border-r border-gray-700 pr-4 mr-2">
            <span class="text-xs font-mono uppercase tracking-widest text-gray-500">
              SIM MODE
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
          
          <!-- Big Toggle Switch -->
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

      <!-- Main Content -->
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
import { Odometer, Monitor, Setting } from '@element-plus/icons-vue'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'

const route = useRoute()
const systemStore = useSystemStore()
const { t, locale } = useI18n()

const currentRouteName = computed(() => {
  switch (route.name) {
    case 'dashboard': return t('layout.missionControl')
    case 'logs': return t('layout.eventStream')
    case 'settings': return t('layout.configuration')
    default: return t('layout.dashboard')
  }
})

function handleToggle(val: string | number | boolean) {
  // Validation Logic
  if (val === true) {
    if (systemStore.settings.ai.provider !== 'Local-Mock' && !systemStore.settings.ai.apiKey) {
      ElMessage.error(t('settings.ai.apiKeyPlaceholder'))
      // Reset switch asynchronously to avoid flicker issue or immediate revert
      setTimeout(() => {
        systemStore.toggleSystem(false)
      }, 0)
      return
    }
  }
  
  systemStore.toggleSystem(val as boolean)
}

function toggleLanguage() {
  locale.value = locale.value === 'en' ? 'zh' : 'en'
}
</script>

<style scoped>
/* Custom overrides for Element Menu to match dark theme */
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

.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
