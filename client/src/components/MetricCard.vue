<template>
  <div class="bg-gray-800/40 border border-gray-700 rounded p-4 flex flex-col justify-between h-32 relative overflow-hidden group hover:border-gray-600 transition-colors">
    <div class="flex justify-between items-start z-10">
      <h3 class="text-xs uppercase tracking-widest text-gray-500 font-bold">{{ title }}</h3>
      <component :is="icon" class="w-5 h-5 text-gray-600 group-hover:text-primary transition-colors" />
    </div>
    
    <div class="z-10 mt-2">
      <div class="text-2xl font-mono font-bold text-white tracking-tight flex items-baseline gap-2">
        <span>{{ value }}</span>
        <span v-if="unit" class="text-xs text-gray-500 font-normal">{{ unit }}</span>
      </div>
      <div v-if="subValue" class="text-xs mt-1" :class="statusColor">
        {{ subValue }}
      </div>
      <!-- Default Slot for extra content like progress bars -->
      <slot />
    </div>

    <!-- Background decoration -->
    <div class="absolute -bottom-4 -right-4 w-24 h-24 bg-gradient-to-br from-gray-700/10 to-transparent rounded-full z-0"></div>
  </div>
</template>

<script setup lang="ts">
import { type Component } from 'vue'

defineProps<{
  title: string
  value: string | number
  unit?: string
  subValue?: string
  statusColor?: string
  icon: Component
}>()
</script>
