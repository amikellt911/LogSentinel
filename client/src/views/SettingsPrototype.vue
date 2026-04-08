<template>
  <div class="h-full flex flex-col overflow-hidden">
    <div class="flex-1 overflow-y-auto custom-scrollbar px-8 pt-8">
      <div class="mx-auto w-full max-w-[1680px] space-y-8 pb-10">
        <div class="border-b border-gray-700 pb-4">
          <!-- 这一版原型页把说明性文案整体拿掉，只保留标题、标签和操作本身。
               用户当前要看的不是字段解释，而是页面结构和可编辑范围，所以这里不再让辅助文字抢视觉注意力。 -->
          <h2 class="text-2xl font-bold text-white tracking-wide">设置原型</h2>
        </div>

        <el-form label-position="top" size="large" class="demo-tabs">
          <el-tabs v-model="activeTab" type="border-card" class="custom-tabs">
            <el-tab-pane name="general">
              <template #label>
                <span class="flex items-center gap-2">
                  <el-icon><Setting /></el-icon>
                  基础
                </span>
              </template>

            <div class="p-6 space-y-8">
              <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">界面与访问</h3>
                <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
                  <el-form-item label="系统语言">
                    <el-select v-model="general.language" class="w-full">
                      <el-option label="English" value="en" />
                      <el-option label="中文 (Chinese)" value="zh" />
                    </el-select>
                  </el-form-item>

                  <el-form-item label="HTTP 端口">
                    <el-input-number v-model="general.httpPort" :min="1024" :max="65535" class="w-full" />
                  </el-form-item>
                </div>
              </div>

              <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">日志保留策略</h3>
                <div class="grid grid-cols-1 gap-6">
                  <el-form-item label="日志保留天数">
                    <el-input-number v-model="general.retentionDays" :min="1" :max="365" class="w-full" />
                  </el-form-item>
                </div>
              </div>
            </div>
          </el-tab-pane>

          <el-tab-pane name="ai">
            <template #label>
              <span class="flex items-center gap-2">
                <el-icon><Cpu /></el-icon>
                AI
              </span>
            </template>

            <!-- AI 页不再写死一个固定高度盒子：
                 上面的 Provider / 稳定性区保持正常可用，下面的 Prompt 工作区按内容自然撑开，
                 页面自己滚动，避免右侧编辑器再被“上面一截 + 下面保存栏”夹成小窗。 -->
            <div class="flex flex-col">
              <div class="bg-[#1a1a1a] border-b border-gray-700 p-6 flex flex-col justify-center shrink-0">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">全局 Provider 配置</h3>
                <div class="grid grid-cols-1 md:grid-cols-4 gap-6">
                  <el-form-item label="默认 Provider">
                    <el-select v-model="ai.provider" class="w-full">
                      <el-option label="Local Mock (Dev)" value="mock" />
                      <el-option label="Google Gemini" value="gemini" />
                      <el-option label="智谱 GLM（后置）" value="zhipu" disabled />
                    </el-select>
                  </el-form-item>

                  <el-form-item label="默认模型">
                    <el-input v-model="ai.model" placeholder="e.g. gemini-2.5-flash" />
                  </el-form-item>

                  <el-form-item label="API Key">
                    <el-input
                      v-model="ai.apiKey"
                      type="password"
                      show-password
                      placeholder="provider api key"
                    />
                  </el-form-item>

                  <el-form-item label="分析输出语言">
                    <el-select v-model="ai.language" class="w-full">
                      <el-option label="English" value="en" />
                      <el-option label="中文 (Chinese)" value="zh" />
                    </el-select>
                  </el-form-item>
                </div>
              </div>

              <div class="bg-[#1a1a1a] border-b border-gray-700 p-6 flex flex-col justify-center shrink-0">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">稳定性策略</h3>
                <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
                  <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                    <div class="flex items-center justify-between mb-4">
                      <span class="text-sm font-bold text-gray-300">自动重试</span>
                      <el-switch v-model="ai.retryEnabled" inline-prompt active-text="ON" inactive-text="OFF" />
                    </div>
                    <el-form-item label="最大重试次数" class="mb-0">
                      <el-input-number
                        v-model="ai.retryMaxAttempts"
                        :min="1"
                        :max="10"
                        class="w-full"
                        :disabled="!ai.retryEnabled"
                      />
                    </el-form-item>
                  </div>

                  <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                    <div class="flex items-center justify-between mb-4">
                      <span class="text-sm font-bold text-gray-300">自动降级</span>
                      <el-switch v-model="ai.autoDegrade" inline-prompt active-text="ON" inactive-text="OFF" />
                    </div>
                    <div class="grid grid-cols-1 gap-4">
                      <el-form-item label="Fallback Provider" class="mb-0">
                        <el-select v-model="ai.fallbackProvider" class="w-full" :disabled="!ai.autoDegrade">
                          <el-option label="Local Mock (Dev)" value="mock" />
                          <el-option label="Google Gemini" value="gemini" />
                          <el-option label="智谱 GLM（后置）" value="zhipu" disabled />
                        </el-select>
                      </el-form-item>
                      <el-form-item label="Fallback Model" class="mb-0">
                        <el-input v-model="ai.fallbackModel" :disabled="!ai.autoDegrade" />
                      </el-form-item>
                    </div>
                  </div>

                  <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                    <div class="flex items-center justify-between mb-4">
                      <span class="text-sm font-bold text-gray-300">熔断</span>
                      <el-switch v-model="ai.circuitBreaker" inline-prompt active-text="ON" inactive-text="OFF" />
                    </div>
                    <div class="grid grid-cols-2 gap-4">
                      <el-form-item label="失败阈值" class="mb-0">
                        <el-input-number
                          v-model="ai.failureThreshold"
                          :min="1"
                          :max="50"
                          class="w-full"
                          :disabled="!ai.circuitBreaker"
                        />
                      </el-form-item>
                      <el-form-item label="冷却时间" class="mb-0">
                        <el-input-number
                          v-model="ai.cooldownSeconds"
                          :min="5"
                          :max="3600"
                          class="w-full"
                          :disabled="!ai.circuitBreaker"
                        >
                          <template #suffix>s</template>
                        </el-input-number>
                      </el-form-item>
                    </div>
                  </div>
                </div>
              </div>

              <!-- prompt 管理区继续严格贴近旧 Settings 的交互：
                   左侧点哪条就编辑哪条，并且同步切成 active，避免再额外造一个“设为 Active”按钮打断原来的使用路径。 -->
              <div class="flex min-h-[780px] flex-col border-t border-gray-700 md:min-h-[860px] md:flex-row">
                <div class="w-full border-r border-gray-700 flex flex-col bg-[#1a1a1a] md:w-[300px] md:min-w-[300px]">
                  <div class="p-4 border-b border-gray-700 flex justify-between items-center bg-[#252525]">
                    <span class="text-sm font-bold text-gray-400 uppercase">业务 Prompt</span>
                    <el-button type="primary" size="small" circle @click="addPrompt">
                      <el-icon><Plus /></el-icon>
                    </el-button>
                  </div>
                  <div class="flex-1 overflow-y-auto custom-scrollbar">
                    <div
                      v-for="prompt in prompts"
                      :key="prompt.id"
                      class="p-4 cursor-pointer hover:bg-gray-800 transition-colors border-b border-gray-800 relative group flex items-center"
                      :class="{ 'bg-gray-800 border-l-2 border-l-primary': selectedPromptId === prompt.id }"
                      @click="handlePromptClick(prompt.id)"
                    >
                      <div class="mr-3 flex items-center justify-center">
                        <div v-if="ai.activePromptId === prompt.id" class="w-2.5 h-2.5 rounded-full bg-green-500 shadow-[0_0_8px_rgba(34,197,94,0.6)]"></div>
                        <div v-else class="w-2.5 h-2.5 rounded-full border border-gray-500"></div>
                      </div>

                      <div class="flex flex-col truncate flex-1 pr-2">
                        <div class="font-mono text-sm text-gray-300 truncate">{{ prompt.name }}</div>
                      </div>

                      <div class="opacity-0 group-hover:opacity-100 transition-opacity ml-auto" v-if="prompts.length > 1">
                        <el-button type="danger" link size="small" @click.stop="removePrompt(prompt.id)">
                          <el-icon><Delete /></el-icon>
                        </el-button>
                      </div>
                    </div>
                  </div>
                </div>

                <div class="w-full min-w-0 bg-[#202020] md:flex-1">
                  <div v-if="selectedPrompt" class="flex h-full min-h-0 flex-col">
                    <!-- 右侧这里改成真正的主工作区：
                         左侧列表只负责切换条目，名称和正文放进同一块编辑面板里，
                         这样用户看过去就知道“这一大块都是可编辑区域”，不会再像被表单壳子分割。 -->
                    <div class="border-b border-gray-700 bg-[#262626] px-6 py-6 md:px-8">
                      <div class="max-w-[420px]">
                        <label class="mb-2 block text-sm font-semibold text-gray-200">Prompt 名称</label>
                        <el-input v-model="selectedPrompt.name" />
                      </div>
                    </div>

                    <!-- 这里不再把正文继续塞进 el-form-item 的默认壳子里：
                         直接把剩余高度整块让给 textarea，视觉上更像正文编辑器，而不是一个被包围盒锁死的小表单。 -->
                    <div class="flex flex-1 min-h-0 flex-col px-6 py-6 md:px-8 md:py-8">
                      <div class="mb-3">
                        <label class="text-sm font-semibold text-gray-200">业务 Prompt 内容</label>
                      </div>

                      <div class="flex-1 min-h-[560px]">
                        <el-input
                          v-model="selectedPrompt.content"
                          type="textarea"
                          :rows="26"
                          class="font-mono text-xs custom-textarea h-full"
                          resize="none"
                        />
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </el-tab-pane>

          <el-tab-pane name="alert">
            <template #label>
              <span class="flex items-center gap-2">
                <el-icon><Share /></el-icon>
                告警
              </span>
            </template>

            <div class="flex flex-col md:flex-row h-[680px] border-t border-gray-700">
              <div class="w-full md:w-[30%] border-r border-gray-700 flex flex-col bg-[#1a1a1a]">
                <div class="p-4 border-b border-gray-700 flex justify-between items-center">
                  <span class="text-sm font-bold text-gray-400 uppercase">飞书渠道</span>
                  <el-button type="primary" size="small" circle @click="addChannel">
                    <el-icon><Plus /></el-icon>
                  </el-button>
                </div>
                <div class="flex-1 overflow-y-auto custom-scrollbar">
                  <div
                    v-for="channel in channels"
                    :key="channel.id"
                    class="p-4 cursor-pointer hover:bg-gray-800 transition-colors border-b border-gray-800 flex justify-between items-center group"
                    :class="{ 'bg-gray-800 border-l-2 border-l-primary': selectedChannelId === channel.id }"
                    @click="selectChannel(channel.id)"
                  >
                    <div class="flex items-center gap-2 truncate">
                      <div class="w-2 h-2 rounded-full" :class="channel.enabled ? 'bg-green-500' : 'bg-gray-600'"></div>
                      <span class="font-mono text-sm text-gray-300 truncate">{{ channel.name }}</span>
                    </div>
                    <div class="flex items-center gap-2">
                      <el-switch v-model="channel.enabled" size="small" @click.stop />
                      <el-button
                        type="danger"
                        link
                        size="small"
                        class="opacity-0 group-hover:opacity-100 transition-opacity"
                        :disabled="channels.length <= 1"
                        @click.stop="removeChannel(channel.id)"
                      >
                        <el-icon><Delete /></el-icon>
                      </el-button>
                    </div>
                  </div>
                </div>
              </div>

              <div class="w-full md:w-[70%] p-6 flex flex-col bg-[#2d2d2d]">
                <div v-if="selectedChannel" class="space-y-6">
                  <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
                    <el-form-item label="渠道名称">
                      <el-input v-model="selectedChannel.name" />
                    </el-form-item>

                    <el-form-item label="Vendor">
                      <el-input model-value="Feishu / Lark" disabled />
                    </el-form-item>
                  </div>

                  <el-form-item label="Webhook URL">
                    <el-input v-model="selectedChannel.webhookUrl" placeholder="https://..." />
                  </el-form-item>

                  <el-form-item label="签名 Secret">
                    <el-input v-model="selectedChannel.secret" type="password" show-password placeholder="未开启签名校验时可以为空" />
                  </el-form-item>

                  <el-form-item label="最小发送等级">
                    <div class="w-full">
                      <el-radio-group
                        v-model="selectedChannel.threshold"
                        class="custom-radio-group"
                        :class="thresholdClass(selectedChannel.threshold)"
                      >
                        <el-radio-button label="critical">Critical</el-radio-button>
                        <el-radio-button label="error">Error</el-radio-button>
                        <el-radio-button label="warning">Warning</el-radio-button>
                      </el-radio-group>
                    </div>
                  </el-form-item>

                  <div class="pt-4 flex justify-end gap-3">
                    <el-button @click="sendChannelProbe">
                      <el-icon class="mr-2"><Promotion /></el-icon>
                      发送测试消息
                    </el-button>
                  </div>
                </div>
              </div>
            </div>
          </el-tab-pane>

          <el-tab-pane name="kernel">
            <template #label>
              <span class="flex items-center gap-2">
                <el-icon><Operation /></el-icon>
                内核 / Trace
              </span>
            </template>

            <div class="p-6 space-y-8">
              <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">核心控制项</h3>
                <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
                  <el-form-item label="工作线程数">
                    <el-input-number v-model="kernel.workerThreads" :min="1" :max="32" class="w-full" />
                  </el-form-item>

                  <el-form-item label="结束标记字段">
                    <el-input v-model="kernel.endFlagField" />
                  </el-form-item>

                  <el-form-item label="结束字段别名">
                    <el-select
                      v-model="kernel.endFlagAliases"
                      class="w-full"
                      multiple
                      filterable
                      allow-create
                      default-first-option
                      collapse-tags
                      collapse-tags-tooltip
                    >
                      <el-option label="end" value="end" />
                      <el-option label="finished" value="finished" />
                      <el-option label="trace_finished" value="trace_finished" />
                    </el-select>
                  </el-form-item>

                  <el-form-item label="Token 打包限额">
                    <el-input-number v-model="kernel.tokenLimit" :min="0" :max="50000" class="w-full" />
                  </el-form-item>

                  <el-form-item label="Span 容量限额">
                    <el-input-number v-model="kernel.spanCapacity" :min="0" :max="50000" class="w-full" />
                  </el-form-item>
                </div>
              </div>

              <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">时间阈值</h3>
                <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
                  <el-form-item label="Collecting 空闲超时">
                    <el-input-number v-model="kernel.collectingIdleMs" :min="100" :max="600000" class="w-full" />
                  </el-form-item>

                  <el-form-item label="Sealed Grace Window">
                    <el-input-number v-model="kernel.sealedGraceMs" :min="50" :max="60000" class="w-full" />
                  </el-form-item>

                  <el-form-item label="重试起始等待时间">
                    <el-input-number v-model="kernel.retryBaseDelayMs" :min="50" :max="60000" class="w-full" />
                  </el-form-item>

                  <el-form-item label="Sweep / Tick 扫描频率">
                    <el-input-number v-model="kernel.sweepTickMs" :min="50" :max="10000" class="w-full" />
                  </el-form-item>
                </div>
              </div>

              <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">水位阈值</h3>
                <div class="grid grid-cols-1 lg:grid-cols-3 gap-6">
                  <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                    <div class="text-sm font-bold text-gray-300 mb-4">Active Sessions</div>
                    <div class="space-y-4">
                      <el-form-item label="Overload" class="mb-0">
                        <el-slider v-model="kernel.watermarks.activeSessions.overload" :min="1" :max="100" show-input />
                      </el-form-item>
                      <el-form-item label="Critical" class="mb-0">
                        <el-slider v-model="kernel.watermarks.activeSessions.critical" :min="1" :max="100" show-input />
                      </el-form-item>
                    </div>
                  </div>

                  <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                    <div class="text-sm font-bold text-gray-300 mb-4">Buffered Spans</div>
                    <div class="space-y-4">
                      <el-form-item label="Overload" class="mb-0">
                        <el-slider v-model="kernel.watermarks.bufferedSpans.overload" :min="1" :max="100" show-input />
                      </el-form-item>
                      <el-form-item label="Critical" class="mb-0">
                        <el-slider v-model="kernel.watermarks.bufferedSpans.critical" :min="1" :max="100" show-input />
                      </el-form-item>
                    </div>
                  </div>

                  <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                    <div class="text-sm font-bold text-gray-300 mb-4">Pending Tasks</div>
                    <div class="space-y-4">
                      <el-form-item label="Overload" class="mb-0">
                        <el-slider v-model="kernel.watermarks.pendingTasks.overload" :min="1" :max="100" show-input />
                      </el-form-item>
                      <el-form-item label="Critical" class="mb-0">
                        <el-slider v-model="kernel.watermarks.pendingTasks.critical" :min="1" :max="100" show-input />
                      </el-form-item>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            </el-tab-pane>
          </el-tabs>
        </el-form>
      </div>
    </div>

    <!-- 底栏这里也只保留动作按钮，不再叠状态说明。
         保存栏的职责就是承载提交/回退动作；状态解释一旦放回来，又会把底部重新做成说明区。 -->
    <div class="shrink-0 border-t border-gray-800 bg-[#141414]/95 backdrop-blur">
      <div class="mx-auto flex w-full max-w-[1680px] justify-end px-8 py-5">
        <div class="flex justify-end gap-3">
          <el-button :disabled="!isDirty" @click="resetPrototype">放弃修改</el-button>
          <el-button type="primary" size="large" :disabled="!isDirty" @click="savePrototype">
            <el-icon class="mr-2"><Check /></el-icon>
            保存原型快照
          </el-button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, reactive, ref } from 'vue'
import { Check, Cpu, Delete, Operation, Plus, Promotion, Setting, Share } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'

type TabId = 'general' | 'ai' | 'alert' | 'kernel'

interface PromptDraft {
  id: number
  name: string
  content: string
}

interface ChannelDraft {
  id: number
  name: string
  enabled: boolean
  webhookUrl: string
  secret: string
  threshold: 'critical' | 'error' | 'warning'
}

interface PrototypeSnapshot {
  general: {
    language: string
    httpPort: number
    retentionDays: number
  }
  ai: {
    provider: string
    model: string
    apiKey: string
    language: string
    retryEnabled: boolean
    retryMaxAttempts: number
    autoDegrade: boolean
    fallbackProvider: string
    fallbackModel: string
    circuitBreaker: boolean
    failureThreshold: number
    cooldownSeconds: number
    activePromptId: number
  }
  prompts: PromptDraft[]
  channels: ChannelDraft[]
  kernel: {
    workerThreads: number
    endFlagField: string
    endFlagAliases: string[]
    tokenLimit: number
    spanCapacity: number
    collectingIdleMs: number
    sealedGraceMs: number
    retryBaseDelayMs: number
    sweepTickMs: number
    watermarks: {
      activeSessions: { overload: number; critical: number }
      bufferedSpans: { overload: number; critical: number }
      pendingTasks: { overload: number; critical: number }
    }
  }
}

/**
 * 这一页故意沿用旧 Settings.vue 的布局骨架：
 * 用户已经明确要“像原来的设置页”，所以这里不再自创新的工作台结构，只替换里面的字段语义和本地原型状态。
 */
const activeTab = ref<TabId>('general')

const general = reactive({
  language: 'zh',
  httpPort: 8080,
  retentionDays: 3
})

const ai = reactive({
  provider: 'gemini',
  model: 'gemini-2.5-flash',
  apiKey: '',
  language: 'zh',
  retryEnabled: true,
  retryMaxAttempts: 3,
  autoDegrade: true,
  fallbackProvider: 'mock',
  fallbackModel: 'mock-trace-analyzer',
  circuitBreaker: true,
  failureThreshold: 5,
  cooldownSeconds: 60,
  activePromptId: 2
})

const prompts = reactive<PromptDraft[]>([
  {
    id: 1,
    name: '支付链路排障',
    content: '重点关注订单支付、银行网关、重试链路和失败回滚，优先给出服务侧排查路径。'
  },
  {
    id: 2,
    name: '基础通用分析',
    content: '优先总结主要异常服务、异常操作、根因方向和建议操作，避免生成过度推测的结论。'
  },
  {
    id: 3,
    name: '风控链路排障',
    content: '关注规则命中、缓存失效、特征拉取和下游依赖超时，给出风控侧和基础设施侧的分层判断。'
  }
])

const channels = reactive<ChannelDraft[]>([
  {
    id: 1,
    name: '生产告警群',
    enabled: true,
    webhookUrl: 'https://open.feishu.cn/open-apis/bot/v2/hook/xxxxxxxx',
    secret: '',
    threshold: 'critical'
  },
  {
    id: 2,
    name: '值班测试群',
    enabled: false,
    webhookUrl: 'https://open.feishu.cn/open-apis/bot/v2/hook/yyyyyyyy',
    secret: 'demo-secret',
    threshold: 'warning'
  }
])

const kernel = reactive({
  workerThreads: 4,
  endFlagField: 'trace_end',
  endFlagAliases: ['end'],
  tokenLimit: 4000,
  spanCapacity: 200,
  collectingIdleMs: 30000,
  sealedGraceMs: 3000,
  retryBaseDelayMs: 500,
  sweepTickMs: 200,
  watermarks: {
    activeSessions: { overload: 70, critical: 90 },
    bufferedSpans: { overload: 75, critical: 92 },
    pendingTasks: { overload: 80, critical: 95 }
  }
})

const selectedPromptId = ref(2)
const selectedChannelId = ref(1)

const selectedPrompt = computed(() => {
  return prompts.find((item) => item.id === selectedPromptId.value) ?? prompts[0]
})

const selectedChannel = computed(() => {
  return channels.find((item) => item.id === selectedChannelId.value) ?? channels[0]
})

function snapshotState(): PrototypeSnapshot {
  return JSON.parse(JSON.stringify({
    general,
    ai,
    prompts,
    channels,
    kernel
  })) as PrototypeSnapshot
}

const savedSnapshot = ref<PrototypeSnapshot>(snapshotState())

/**
 * 保存区仍然做脏状态判断，因为用户要的是“像真页”的交互。
 * 这里虽然还没接后端，但至少要把本地改动、重启字段和恢复操作都做成和真实设置页一致的使用感。
 */
const isDirty = computed(() => JSON.stringify(snapshotState()) !== JSON.stringify(savedSnapshot.value))
const restartRequired = computed(() => {
  return general.httpPort !== savedSnapshot.value.general.httpPort
    || kernel.workerThreads !== savedSnapshot.value.kernel.workerThreads
})

function ensurePromptSelection() {
  if (!prompts.some((item) => item.id === selectedPromptId.value)) {
    selectedPromptId.value = prompts[0]?.id ?? 0
  }
}

function ensureChannelSelection() {
  if (!channels.some((item) => item.id === selectedChannelId.value)) {
    selectedChannelId.value = channels[0]?.id ?? 0
  }
}

function applySnapshot(next: PrototypeSnapshot) {
  Object.assign(general, next.general)
  Object.assign(ai, next.ai)
  prompts.splice(0, prompts.length, ...next.prompts)
  channels.splice(0, channels.length, ...next.channels)
  Object.assign(kernel, next.kernel)
  ensurePromptSelection()
  ensureChannelSelection()
}

function savePrototype() {
  const restartTouched = restartRequired.value
  savedSnapshot.value = snapshotState()
  ElMessage.success(restartTouched ? '原型快照已保存：包含重启后生效字段' : '原型快照已保存（仅本地）')
}

function resetPrototype() {
  applySnapshot(JSON.parse(JSON.stringify(savedSnapshot.value)) as PrototypeSnapshot)
  ElMessage.info('已恢复到上一次保存的原型快照')
}

/**
 * 这里故意沿用旧 Settings 的点击语义：
 * 用户点左侧列表项时，同时完成“选中编辑”和“切成 active”，这样视觉状态点和右侧编辑目标始终一致。
 */
function handlePromptClick(id: number) {
  selectedPromptId.value = id
  ai.activePromptId = id
}

function addPrompt() {
  const nextId = Math.max(...prompts.map((item) => item.id), 0) + 1
  prompts.unshift({
    id: nextId,
    name: `新业务 Prompt ${nextId}`,
    content: '在这里填写业务背景、领域术语和关注重点。'
  })
  selectedPromptId.value = nextId
}

function removePrompt(id: number) {
  if (prompts.length <= 1) return
  const index = prompts.findIndex((item) => item.id === id)
  if (index < 0) return
  prompts.splice(index, 1)
  if (ai.activePromptId === id) {
    ai.activePromptId = prompts[0].id
  }
  ensurePromptSelection()
}

function selectChannel(id: number) {
  selectedChannelId.value = id
}

function addChannel() {
  const nextId = Math.max(...channels.map((item) => item.id), 0) + 1
  channels.push({
    id: nextId,
    name: `新飞书渠道 ${nextId}`,
    enabled: false,
    webhookUrl: '',
    secret: '',
    threshold: 'error'
  })
  selectedChannelId.value = nextId
}

function removeChannel(id: number) {
  if (channels.length <= 1) return
  const index = channels.findIndex((item) => item.id === id)
  if (index < 0) return
  channels.splice(index, 1)
  ensureChannelSelection()
}

function thresholdClass(level: string) {
  if (level === 'critical') return 'is-critical'
  if (level === 'error') return 'is-error'
  if (level === 'warning') return 'is-warning'
  return ''
}

/**
 * 测试发送和保存原型快照必须分开：
 * 一个是在验证单条飞书渠道配置能不能发，另一个是在保存整页设置状态，两个动作语义完全不是一回事。
 */
function sendChannelProbe() {
  if (!selectedChannel.value?.webhookUrl.trim()) {
    ElMessage.warning('请先填写 Webhook URL，再发送测试消息')
    return
  }

  const secretState = selectedChannel.value.secret.trim() ? '已附带签名 Secret' : '未填写签名 Secret'
  ElMessage.success(`已模拟发送测试消息：${selectedChannel.value.name}（${secretState}）`)
}
</script>

<style scoped>
:deep(.el-tabs--border-card) {
  background-color: transparent;
  border-color: #374151;
}

:deep(.el-tabs--border-card > .el-tabs__header) {
  background-color: rgba(30, 30, 30, 0.5);
  border-bottom-color: #374151;
}

:deep(.el-tabs--border-card > .el-tabs__header .el-tabs__item.is-active) {
  background-color: #2d2d2d;
  border-right-color: #374151;
  border-left-color: #374151;
  color: #409eff;
}

:deep(.el-tabs--border-card > .el-tabs__header .el-tabs__item) {
  color: #9ca3af;
}

:deep(.el-tabs--border-card > .el-tabs__content) {
  background-color: #2d2d2d;
  padding: 0;
}

/* Prompt 主编辑区必须让 Element Plus 的组件根、textarea 包裹层、inner 三层一起继承高度。
   否则外层面板看起来已经被拉高了，真正能输入的那层还是会缩回默认高度，用户就会继续觉得编辑区被锁死。 */
:deep(.custom-textarea) {
  height: 100%;
}

:deep(.custom-textarea .el-textarea) {
  height: 100%;
  display: flex;
}

:deep(.custom-textarea .el-textarea__inner) {
  background-color: #1a1a1a !important;
  color: #a8a29e !important;
  border-color: #3f3f46 !important;
  padding: 18px 20px;
  font-family: 'Fira Code', monospace;
  font-size: 0.8rem;
  height: 100% !important;
  min-height: 100% !important;
  overflow-y: auto !important;
  line-height: 1.75;
}

:deep(.custom-radio-group.is-critical .el-radio-button__original-radio:checked + .el-radio-button__inner) {
  background-color: #ef4444 !important;
  border-color: #ef4444 !important;
  box-shadow: -1px 0 0 0 #ef4444 !important;
}

:deep(.custom-radio-group.is-error .el-radio-button__original-radio:checked + .el-radio-button__inner) {
  background-color: #f97316 !important;
  border-color: #f97316 !important;
  box-shadow: -1px 0 0 0 #f97316 !important;
}

:deep(.custom-radio-group.is-warning .el-radio-button__original-radio:checked + .el-radio-button__inner) {
  background-color: #eab308 !important;
  border-color: #eab308 !important;
  box-shadow: -1px 0 0 0 #eab308 !important;
  color: #1f2937 !important;
}

.custom-scrollbar::-webkit-scrollbar {
  width: 8px;
}

.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #4b5563;
  border-radius: 4px;
}
</style>
