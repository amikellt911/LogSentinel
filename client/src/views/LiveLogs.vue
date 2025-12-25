<template>
  <div class="h-full flex flex-col bg-black font-mono text-sm relative">
    <!-- Terminal Header -->
    <div class="flex items-center justify-between px-4 py-2 bg-gray-900 border-b border-gray-800">
      <div class="flex items-center gap-2">
        <div class="flex gap-1.5">
          <div class="w-3 h-3 rounded-full bg-red-500/80"></div>
          <div class="w-3 h-3 rounded-full bg-yellow-500/80"></div>
          <div class="w-3 h-3 rounded-full bg-green-500/80"></div>
        </div>
        <span class="ml-2 text-gray-500 text-xs">/var/log/sentinel/stream.log</span>
      </div>
      <div class="text-xs text-gray-500">
        {{ systemStore.logs.length }} {{ $t('logs.buffered') }}
      </div>
    </div>

    <!-- Logs Container -->
    <div 
      ref="logContainer" 
      class="flex-1 overflow-y-auto p-4 space-y-1 scroll-smooth"
    >
      <div
        v-for="log in systemStore.logs"
        :key="log.id"
        class="flex gap-3 hover:bg-gray-900/50 p-0.5 rounded leading-tight transition-colors duration-150"
        :class="{
          'bg-red-900/20 animate-pulse-slow border-l-2 border-red-500 text-red-100': log.level === 'Critical',
          'bg-orange-900/20 border-l-2 border-orange-500 text-orange-100': log.level === 'Error',
          'bg-yellow-900/20 border-l-2 border-yellow-500 text-yellow-100': log.level === 'Warning',
          'bg-blue-900/20 border-l-2 border-blue-500 text-blue-100': log.level === 'Info',
          'bg-green-900/20 border-l-2 border-green-500 text-green-100': log.level === 'Safe',
          'bg-gray-800/50 border-l-2 border-gray-600 text-gray-400': log.level === 'Unknown'
        }"
      >
        <!-- Timestamp -->
        <span class="text-gray-600 shrink-0 select-none">[{{ log.timestamp }}]</span>

        <!-- Level -->
        <span
          class="font-bold shrink-0 w-20 text-center select-none"
          :class="{
            'text-red-500': log.level === 'Critical',
            'text-orange-500': log.level === 'Error',
            'text-yellow-500': log.level === 'Warning',
            'text-blue-400': log.level === 'Info',
            'text-green-500': log.level === 'Safe',
            'text-gray-500': log.level === 'Unknown'
          }"
        >
          {{ log.level }}
        </span>

        <!-- Message -->
        <span class="break-all whitespace-pre-wrap">{{ log.message }}</span>
      </div>
      
      <!-- Anchor for scrolling -->
      <div ref="scrollAnchor"></div>
    </div>
    
    <!-- Status Footer -->
    <div class="px-4 py-1 bg-gray-900 border-t border-gray-800 text-xs text-green-500 flex items-center gap-2">
      <span class="w-2 h-2 bg-green-500 rounded-full animate-pulse" v-if="systemStore.isRunning"></span>
      <span v-else class="w-2 h-2 bg-gray-500 rounded-full"></span>
      <span>{{ systemStore.isRunning ? $t('logs.receiving') : $t('logs.paused') }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, nextTick, onMounted, onUnmounted } from 'vue'
import { useSystemStore } from '../stores/system'

const systemStore = useSystemStore()
const scrollAnchor = ref<HTMLElement | null>(null)

// Auto-scroll logic
const scrollToBottom = () => {
  if (scrollAnchor.value) {
    scrollAnchor.value.scrollIntoView({ behavior: 'auto' }) // 'auto' is faster/less jarring for logs than 'smooth'
  }
}

// Watch logs and scroll
watch(() => systemStore.logs.length, () => {
  nextTick(() => {
    scrollToBottom()
  })
})

onMounted(() => {
  scrollToBottom()
  // Start explicit polling for this view
  if (systemStore.isRunning) {
      systemStore.startLogPolling()
  }
})

// Stop polling when leaving this view
onUnmounted(() => {
    systemStore.stopLogPolling()
})

// Also watch isRunning to toggle polling if changed while on this page
watch(() => systemStore.isRunning, (newVal) => {
    if (newVal) systemStore.startLogPolling()
    else systemStore.stopLogPolling()
})
</script>

<style scoped>
/* Custom scrollbar to match terminal aesthetic */
div::-webkit-scrollbar {
  width: 10px;
}

div::-webkit-scrollbar-track {
  background: #000;
}

div::-webkit-scrollbar-thumb {
  background: #333;
  border: 2px solid #000;
  border-radius: 0;
}

div::-webkit-scrollbar-thumb:hover {
  background: #555;
}

.animate-pulse-slow {
  animation: pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: .7;
  }
}
</style>
