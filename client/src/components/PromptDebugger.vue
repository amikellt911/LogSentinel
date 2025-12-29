<template>
  <el-drawer
    v-model="visible"
    :title="$t('aiEngine.promptDebugger.title') + ' - Batch #' + (data?.batchId || '???')"
    direction="rtl"
    size="60%"
    :before-close="handleClose"
    class="prompt-debugger-drawer"
  >
    <div class="flex flex-col h-full overflow-hidden" v-if="data">

      <!-- 元数据头部 -->
      <div class="flex items-center gap-4 mb-4 pb-4 border-b border-gray-700">
        <el-tag effect="dark" type="info">{{ data.meta.model }}</el-tag>
        <span class="text-xs text-gray-500 font-mono">{{ data.meta.timestamp }}</span>
        <div class="ml-auto flex gap-4 text-xs font-mono">
           <span class="text-blue-400">Latency: {{ data.meta.latency }}ms</span>
           <span class="text-purple-400">Tokens: {{ data.meta.tokens }}</span>
        </div>
      </div>

      <!-- 内容区域 -->
      <div class="flex-1 flex gap-4 overflow-hidden min-h-0">

        <!-- 输入 (左侧) -->
        <div class="flex-1 flex flex-col min-w-0">
           <div class="text-xs font-bold text-gray-400 mb-2 uppercase tracking-wider">{{ $t('aiEngine.promptDebugger.input') }} (JSON)</div>
           <div class="flex-1 bg-[#1e1e1e] border border-gray-700 rounded p-4 overflow-auto custom-scrollbar">
              <pre class="text-xs font-mono text-gray-300 whitespace-pre-wrap">{{ JSON.stringify(data.input, null, 2) }}</pre>
           </div>
        </div>

        <!-- 输出 (右侧) -->
        <div class="flex-1 flex flex-col min-w-0">
           <div class="text-xs font-bold text-purple-400 mb-2 uppercase tracking-wider">{{ $t('aiEngine.promptDebugger.output') }} (JSON)</div>
           <div class="flex-1 bg-[#1e1e1e] border border-gray-700 rounded p-4 overflow-auto custom-scrollbar">
               <pre class="text-xs font-mono text-green-300 whitespace-pre-wrap">{{ JSON.stringify(data.output, null, 2) }}</pre>
           </div>
        </div>

      </div>

      <!-- 底部操作 -->
      <div class="mt-4 flex justify-end">
          <el-button type="primary" @click="copyJson" size="small">{{ $t('aiEngine.promptDebugger.copy') }}</el-button>
      </div>

    </div>
    <div v-else class="h-full flex items-center justify-center text-gray-500">
        {{ $t('aiEngine.promptDebugger.noData') }}
    </div>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { PromptDebugInfo } from '../types/aiEngine'
import { ElMessage } from 'element-plus'
import { useI18n } from 'vue-i18n'

const props = defineProps<{
  modelValue: boolean
  data: PromptDebugInfo | null
}>()

const emit = defineEmits(['update:modelValue'])
const { t } = useI18n()

// 双向绑定可见性
const visible = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val)
})

const handleClose = (done: () => void) => {
  done()
}

// 复制 JSON 内容到剪贴板
const copyJson = () => {
    if (!props.data) return
    const content = JSON.stringify({ input: props.data.input, output: props.data.output }, null, 2)
    navigator.clipboard.writeText(content).then(() => {
        ElMessage.success(t('aiEngine.promptDebugger.copySuccess'))
    })
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
.custom-scrollbar::-webkit-scrollbar-track {
  background: #111827;
}

:deep(.el-drawer__body) {
  padding: 20px;
  background-color: #1a1a1a;
}
:deep(.el-drawer__header) {
  margin-bottom: 0;
  padding: 20px;
  border-bottom: 1px solid #374151;
  color: #e5e7eb;
}
</style>
