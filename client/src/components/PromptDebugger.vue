<template>
  <el-drawer
    :model-value="modelValue"
    :title="drawerTitle"
    direction="rtl"
    :size="drawerSize"
    @update:model-value="handleClose"
    class="prompt-debugger-drawer"
  >
    <template v-if="promptDebugInfo">
      <!-- 元数据 -->
      <div class="mb-4 p-3 bg-gray-900/50 border border-gray-700 rounded-lg">
        <div class="grid grid-cols-2 md:grid-cols-4 gap-4 text-sm">
          <div>
            <span class="text-gray-500">{{ $t('aiEngine.model') }}:</span>
            <span class="ml-2 text-blue-400 font-mono">{{ promptDebugInfo.metadata.model }}</span>
          </div>
          <div>
            <span class="text-gray-500">{{ $t('aiEngine.duration') }}:</span>
            <span class="ml-2 text-green-400 font-mono">{{ promptDebugInfo.metadata.duration }}ms</span>
          </div>
          <div>
            <span class="text-gray-500">{{ $t('aiEngine.totalTokens') }}:</span>
            <span class="ml-2 text-purple-400 font-mono">{{ promptDebugInfo.metadata.total_tokens }}</span>
          </div>
          <div>
            <span class="text-gray-500">{{ $t('aiEngine.timestamp') }}:</span>
            <span class="ml-2 text-gray-400 font-mono">{{ promptDebugInfo.metadata.timestamp }}</span>
          </div>
        </div>
      </div>

      <!-- 左右分栏布局 -->
      <div class="flex flex-col lg:flex-row gap-4 h-full">
        <!-- 左侧：Input -->
        <div class="flex-1 flex flex-col min-h-0">
          <div class="flex items-center justify-between mb-2">
            <h3 class="text-sm font-bold text-gray-300 flex items-center gap-2">
              <el-icon class="text-blue-400"><Upload /></el-icon>
              {{ $t('aiEngine.input') }}
            </h3>
            <el-button size="small" type="primary" link @click="copyInput">
              <el-icon><CopyDocument /></el-icon>
              {{ $t('common.copy') }}
            </el-button>
          </div>
          <div class="flex-1 bg-gray-900 border border-gray-700 rounded-lg overflow-auto custom-scrollbar">
            <pre class="json-container p-4 text-xs font-mono"><code :class="languageClass" v-html="highlightedInput"></code></pre>
          </div>
        </div>

        <!-- 右侧：Output -->
        <div class="flex-1 flex flex-col min-h-0">
          <div class="flex items-center justify-between mb-2">
            <h3 class="text-sm font-bold text-gray-300 flex items-center gap-2">
              <el-icon class="text-green-400"><Download /></el-icon>
              {{ $t('aiEngine.output') }}
            </h3>
            <el-button size="small" type="primary" link @click="copyOutput">
              <el-icon><CopyDocument /></el-icon>
              {{ $t('common.copy') }}
            </el-button>
          </div>
          <div class="flex-1 bg-gray-900 border border-gray-700 rounded-lg overflow-auto custom-scrollbar">
            <pre class="json-container p-4 text-xs font-mono"><code :class="languageClass" v-html="highlightedOutput"></code></pre>
          </div>
        </div>
      </div>
    </template>
  </el-drawer>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Upload, Download, CopyDocument } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import type { PromptDebugInfo } from '../types/trace'

const { t } = useI18n()

const props = defineProps<{
  modelValue: boolean
  promptDebugInfo: PromptDebugInfo | null
  traceId: string
}>()

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
}>()

/**
 * 抽屉宽度（自适应）
 */
const drawerSize = computed(() => {
  if (window.innerWidth >= 1024) return '80%'
  if (window.innerWidth >= 768) return '90%'
  return '100%'
})

/**
 * 抽屉标题
 */
const drawerTitle = computed(() => {
  return `${props.traceId} - ${props.promptDebugInfo?.metadata.model || ''}`
})

/**
 * 语言类（用于 Prism.js 高亮）
 */
const languageClass = 'language-json'

/**
 * 高亮的 Input JSON
 */
const highlightedInput = computed(() => {
  if (!props.promptDebugInfo) return ''
  return syntaxHighlight(props.promptDebugInfo.input)
})

/**
 * 高亮的 Output JSON
 */
const highlightedOutput = computed(() => {
  if (!props.promptDebugInfo) return ''
  return syntaxHighlight(props.promptDebugInfo.output)
})

/**
 * JSON 语法高亮（简单实现）
 * TODO: 可考虑使用 Prism.js 或 highlight.js 替换
 */
function syntaxHighlight(json: any): string {
  if (typeof json !== 'string') {
    json = JSON.stringify(json, null, 2)
  }

  json = json.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')

  return json.replace(/("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+\-]?\d+)?)/g, function (match) {
    let cls = 'text-purple-400' // number
    if (/^"/.test(match)) {
      if (/:$/.test(match)) {
        cls = 'text-blue-400' // key
      } else {
        cls = 'text-green-400' // string
      }
    } else if (/true|false/.test(match)) {
      cls = 'text-red-400' // boolean
    } else if (/null/.test(match)) {
      cls = 'text-gray-500' // null
    }
    return `<span class="${cls}">${match}</span>`
  })
}

/**
 * 复制 Input
 */
function copyInput() {
  if (!props.promptDebugInfo) return
  copyToClipboard(JSON.stringify(props.promptDebugInfo.input, null, 2))
}

/**
 * 复制 Output
 */
function copyOutput() {
  if (!props.promptDebugInfo) return
  copyToClipboard(JSON.stringify(props.promptDebugInfo.output, null, 2))
}

/**
 * 复制到剪贴板
 */
function copyToClipboard(text: string) {
  navigator.clipboard.writeText(text).then(() => {
    ElMessage.success(t('common.copySuccess'))
  }).catch(() => {
    ElMessage.error(t('common.copyFailed'))
  })
}

/**
 * 关闭抽屉
 */
function handleClose() {
  emit('update:modelValue', false)
}
</script>

<style scoped>
.prompt-debugger-drawer :deep(.el-drawer__header) {
  margin-bottom: 0;
  padding-bottom: 16px;
  border-bottom: 1px solid #374151;
}

.prompt-debugger-drawer :deep(.el-drawer__body) {
  padding: 16px;
}

.custom-scrollbar::-webkit-scrollbar {
  width: 8px;
  height: 8px;
}

.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #374151;
  border-radius: 4px;
}

.custom-scrollbar::-webkit-scrollbar-thumb:hover {
  background: #4b5563;
}

.custom-scrollbar::-webkit-scrollbar-track {
  background: #1f2937;
}

.json-container {
  line-height: 1.6;
}

/* JSON 语法高亮样式（Tailwind CSS 类） */
.text-purple-400 {
  color: #c084fc;
}

.text-blue-400 {
  color: #60a5fa;
}

.text-green-400 {
  color: #4ade80;
}

.text-red-400 {
  color: #f87171;
}

.text-gray-500 {
  color: #6b7280;
}
</style>
