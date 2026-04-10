<template>
  <div class="h-full flex flex-col overflow-hidden">
    <div class="flex-1 overflow-y-auto custom-scrollbar px-8 pt-8">
      <div class="mx-auto w-full max-w-[1680px] space-y-8 pb-10">
        <div class="border-b border-gray-700 pb-4">
          <!-- 这一版原型页把说明性文案整体拿掉，只保留标题、标签和操作本身。
               用户当前要看的不是字段解释，而是页面结构和可编辑范围，所以这里不再让辅助文字抢视觉注意力。 -->
          <h2 class="text-2xl font-bold text-white tracking-wide">设置</h2>
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
                  <!-- 这个总开关控制的是 Trace 主链是否真的发起 AI 分析。
                       关闭后 provider/model/key 仍然保留，但后端 worker 只会把 ai_status 记成 skipped_manual。 -->
                  <el-form-item label="AI 分析开关">
                    <el-switch v-model="ai.analysisEnabled" inline-prompt active-text="ON" inactive-text="OFF" />
                  </el-form-item>

                  <el-form-item label="默认 Provider">
                    <el-select v-model="ai.provider" class="w-full" :disabled="!ai.analysisEnabled">
                      <el-option label="Local Mock (Dev)" value="mock" />
                      <el-option label="Google Gemini" value="gemini" />
                      <el-option label="智谱 GLM（后置）" value="zhipu" disabled />
                    </el-select>
                  </el-form-item>

                  <el-form-item label="默认模型">
                    <el-input v-model="ai.model" placeholder="e.g. gemini-2.5-flash" :disabled="!ai.analysisEnabled" />
                  </el-form-item>

                  <el-form-item label="API Key">
                    <el-input
                      v-model="ai.apiKey"
                      type="password"
                      show-password
                      placeholder="provider api key"
                      :disabled="!ai.analysisEnabled"
                    />
                  </el-form-item>

                  <el-form-item label="分析输出语言">
                    <el-select v-model="ai.language" class="w-full" :disabled="!ai.analysisEnabled">
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
                      <!-- 降级配置这里单独放一份 key，不和默认 provider 共用。
                           既然 fallback 允许切供应商/切账号，那么用户就必须能明确填自己的备用凭证。 -->
                      <el-form-item label="Fallback API Key" class="mb-0">
                        <el-input
                          v-model="ai.fallbackApiKey"
                          type="password"
                          show-password
                          :disabled="!ai.autoDegrade"
                        />
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

              <!-- prompt 管理区现在把“选中编辑”和“设为生效”彻底拆开：
                   左侧点击只表示当前正在编辑哪条，active 由右侧独立按钮决定，避免用户把“我点到了这条”误解成“这条已经会生效”。 -->
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
                      :class="{
                        'bg-gray-800 border-l-2 border-l-primary': selectedPromptId === prompt.id,
                        'ring-1 ring-emerald-500/50 ring-inset bg-emerald-950/20': ai.activePromptId === prompt.id
                      }"
                      @click="handlePromptClick(prompt.id)"
                    >
                      <div class="mr-3 flex items-center justify-center">
                        <!-- selected 和 active 必须拆开。
                             selected 只表示“右侧当前正在编辑哪一条”，active 才表示“重启后真正会生效哪一条”，
                             所以这里不再只靠绿色小点，而是额外给 active 整行 badge 和边框状态。 -->
                        <div
                          v-if="ai.activePromptId === prompt.id"
                          class="w-2.5 h-2.5 rounded-full bg-emerald-400 shadow-[0_0_10px_rgba(52,211,153,0.75)]"
                        ></div>
                        <div v-else-if="selectedPromptId === prompt.id" class="w-2.5 h-2.5 rounded-full bg-sky-400 shadow-[0_0_8px_rgba(56,189,248,0.55)]"></div>
                        <div v-else class="w-2.5 h-2.5 rounded-full border border-gray-500"></div>
                      </div>

                      <div class="flex flex-col truncate flex-1 pr-2">
                        <div class="flex items-center gap-2">
                          <div class="font-mono text-sm text-gray-300 truncate">{{ prompt.name }}</div>
                          <span
                            v-if="ai.activePromptId === prompt.id"
                            class="shrink-0 rounded-full border border-emerald-500/60 bg-emerald-500/15 px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wider text-emerald-300"
                          >
                            {{ activePromptBadgeLabel }}
                          </span>
                        </div>
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
                    <div class="border-b border-gray-700 bg-[#262626] px-6 py-6 md:px-8">
                      <div class="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
                        <div class="max-w-[420px]">
                          <label class="mb-2 block text-sm font-semibold text-gray-200">Prompt 名称</label>
                          <el-input v-model="selectedPrompt.name" />
                        </div>

                        <div class="flex flex-col items-start gap-2 lg:items-end">
                          <el-button
                            type="primary"
                            :disabled="!selectedPrompt || ai.activePromptId === selectedPrompt.id"
                            @click="setSelectedPromptActive"
                          >
                            <el-icon class="mr-2"><Check /></el-icon>
                            {{ promptEditorLabels.setActive }}
                          </el-button>
                          <div class="text-xs text-gray-400">{{ promptEditorLabels.activeHint }}</div>
                        </div>
                      </div>
                    </div>

                    <div class="flex flex-1 min-h-0 flex-col xl:flex-row">
                      <div class="flex-1 min-h-0 overflow-y-auto custom-scrollbar px-6 py-6 md:px-8 md:py-8">
                        <!-- 业务 Prompt 不再给一个自由大文本框。
                             这里按数据形态拆成单值、术语字典、多条规则三种控件，目的是把“用户能改什么”直接写进编辑器结构里。 -->
                        <div class="space-y-6">
                          <div class="rounded border border-gray-700 bg-[#1a1a1a] p-5">
                            <div class="mb-3 text-sm font-semibold text-gray-200">{{ promptEditorLabels.domainGoal }}</div>
                            <el-input
                              v-model="selectedPrompt.content.domainGoal"
                              type="textarea"
                              :rows="3"
                              resize="none"
                              placeholder="例如：支付链路异常分析"
                            />
                          </div>

                          <div class="rounded border border-gray-700 bg-[#1a1a1a] p-5">
                            <div class="mb-4 flex items-center justify-between">
                              <div class="text-sm font-semibold text-gray-200">{{ promptEditorLabels.businessGlossary }}</div>
                              <el-button size="small" @click="addPromptGlossaryRow">
                                <el-icon class="mr-1"><Plus /></el-icon>
                                添加术语
                              </el-button>
                            </div>
                            <div class="space-y-3">
                              <div
                                v-for="(row, index) in selectedPrompt.content.businessGlossary"
                                :key="`glossary-${index}`"
                                class="grid grid-cols-1 gap-3 rounded border border-gray-800 bg-[#202020] p-3 md:grid-cols-[minmax(180px,240px)_1fr_auto]"
                              >
                                <el-input v-model="row.term" placeholder="term" />
                                <el-input v-model="row.meaning" placeholder="meaning" />
                                <div class="flex items-center justify-end">
                                  <el-button
                                    type="danger"
                                    link
                                    :disabled="selectedPrompt.content.businessGlossary.length <= 1"
                                    @click="removePromptGlossaryRow(index)"
                                  >
                                    <el-icon><Delete /></el-icon>
                                  </el-button>
                                </div>
                              </div>
                            </div>
                          </div>

                          <div class="rounded border border-gray-700 bg-[#1a1a1a] p-5">
                            <div class="mb-4 flex items-center justify-between">
                              <div class="text-sm font-semibold text-gray-200">{{ promptEditorLabels.focusAreas }}</div>
                              <el-button size="small" @click="addPromptListRow('focusAreas')">
                                <el-icon class="mr-1"><Plus /></el-icon>
                                添加关注点
                              </el-button>
                            </div>
                            <div class="space-y-3">
                              <div
                                v-for="(_, index) in selectedPrompt.content.focusAreas"
                                :key="`focus-${index}`"
                                class="flex items-center gap-3 rounded border border-gray-800 bg-[#202020] p-3"
                              >
                                <el-input v-model="selectedPrompt.content.focusAreas[index]" placeholder="例如：timeout" />
                                <el-button
                                  type="danger"
                                  link
                                  :disabled="selectedPrompt.content.focusAreas.length <= 1"
                                  @click="removePromptListRow('focusAreas', index)"
                                >
                                  <el-icon><Delete /></el-icon>
                                </el-button>
                              </div>
                            </div>
                          </div>

                          <div class="rounded border border-gray-700 bg-[#1a1a1a] p-5">
                            <div class="mb-4 flex items-center justify-between">
                              <div class="text-sm font-semibold text-gray-200">{{ promptEditorLabels.riskPreference }}</div>
                              <el-button size="small" @click="addPromptListRow('riskPreference')">
                                <el-icon class="mr-1"><Plus /></el-icon>
                                添加偏好
                              </el-button>
                            </div>
                            <div class="space-y-3">
                              <div
                                v-for="(_, index) in selectedPrompt.content.riskPreference"
                                :key="`risk-${index}`"
                                class="flex items-center gap-3 rounded border border-gray-800 bg-[#202020] p-3"
                              >
                                <el-input v-model="selectedPrompt.content.riskPreference[index]" placeholder="例如：涉及资金安全的问题，风险等级从严判断。" />
                                <el-button
                                  type="danger"
                                  link
                                  :disabled="selectedPrompt.content.riskPreference.length <= 1"
                                  @click="removePromptListRow('riskPreference', index)"
                                >
                                  <el-icon><Delete /></el-icon>
                                </el-button>
                              </div>
                            </div>
                          </div>

                          <div class="rounded border border-gray-700 bg-[#1a1a1a] p-5">
                            <div class="mb-4 flex items-center justify-between">
                              <div class="text-sm font-semibold text-gray-200">{{ promptEditorLabels.outputPreference }}</div>
                              <el-button size="small" @click="addPromptListRow('outputPreference')">
                                <el-icon class="mr-1"><Plus /></el-icon>
                                添加输出要求
                              </el-button>
                            </div>
                            <div class="space-y-3">
                              <div
                                v-for="(_, index) in selectedPrompt.content.outputPreference"
                                :key="`output-${index}`"
                                class="flex items-center gap-3 rounded border border-gray-800 bg-[#202020] p-3"
                              >
                                <el-input v-model="selectedPrompt.content.outputPreference[index]" placeholder="例如：summary 要短，solution 要偏操作建议。" />
                                <el-button
                                  type="danger"
                                  link
                                  :disabled="selectedPrompt.content.outputPreference.length <= 1"
                                  @click="removePromptListRow('outputPreference', index)"
                                >
                                  <el-icon><Delete /></el-icon>
                                </el-button>
                              </div>
                            </div>
                          </div>
                        </div>
                      </div>

                      <div class="border-t border-gray-700 bg-[#181818] p-6 xl:w-[420px] xl:border-l xl:border-t-0">
                        <!-- 左侧标题跟着界面语言切换，但右侧预览故意保留英文结构。
                             因为这里展示的不是 UI 文案，而是后面真正会被渲染成 business_guidance 的 prompt 片段。 -->
                        <div class="mb-3 text-sm font-semibold text-gray-200">{{ promptEditorLabels.preview }}</div>
                        <div class="rounded border border-gray-700 bg-[#111111] p-4">
                          <pre class="whitespace-pre-wrap break-words text-xs leading-6 text-gray-300 font-mono min-h-[480px]">{{ selectedPromptPreview || '当前还没有可预览的业务 guidance。' }}</pre>
                        </div>
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
          <el-button :disabled="!isDirty || isLoading || isSaving" @click="resetPrototype">放弃修改</el-button>
          <el-button type="primary" size="large" :disabled="!isDirty || isLoading" :loading="isSaving" @click="savePrototype">
            <el-icon class="mr-2"><Check /></el-icon>
            保存设置
          </el-button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { Check, Cpu, Delete, Operation, Plus, Promotion, Setting, Share } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import i18n from '../i18n'

type TabId = 'general' | 'ai' | 'alert' | 'kernel'

interface PromptDraft {
  id: number
  name: string
  content: PromptContentDraft
}

interface PromptGlossaryDraft {
  term: string
  meaning: string
}

interface PromptContentDraft {
  domainGoal: string
  businessGlossary: PromptGlossaryDraft[]
  focusAreas: string[]
  riskPreference: string[]
  outputPreference: string[]
}

interface ChannelDraft {
  id: number
  name: string
  enabled: boolean
  webhookUrl: string
  secret: string
  threshold: 'critical' | 'error' | 'warning'
}

interface BackendPromptConfig {
  id: number
  name: string
  content: string
  is_active: number | boolean
}

interface BackendChannelConfig {
  id: number
  name: string
  provider: string
  webhook_url: string
  secret?: string
  alert_threshold: 'critical' | 'error' | 'warning'
  is_active: number | boolean
}

interface BackendSettingsResponse {
  config: Record<string, unknown>
  prompts: BackendPromptConfig[]
  channels: BackendChannelConfig[]
}

interface PrototypeSnapshot {
  general: {
    language: string
    httpPort: number
    retentionDays: number
  }
  ai: {
    analysisEnabled: boolean
    provider: string
    model: string
    apiKey: string
    language: string
    retryEnabled: boolean
    retryMaxAttempts: number
    autoDegrade: boolean
    fallbackProvider: string
    fallbackModel: string
    fallbackApiKey: string
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
const isLoading = ref(false)
const isSaving = ref(false)

const general = reactive({
  language: 'zh',
  httpPort: 8080,
  retentionDays: 3
})

const ai = reactive({
  analysisEnabled: true,
  provider: 'gemini',
  model: 'gemini-2.5-flash',
  apiKey: '',
  language: 'zh',
  retryEnabled: true,
  retryMaxAttempts: 3,
  autoDegrade: true,
  fallbackProvider: 'mock',
  fallbackModel: 'mock-trace-analyzer',
  fallbackApiKey: '',
  circuitBreaker: true,
  failureThreshold: 5,
  cooldownSeconds: 60,
  activePromptId: 2
})

const prompts = reactive<PromptDraft[]>([
  {
    id: 1,
    name: '支付链路排障',
    content: {
      domainGoal: '支付链路异常分析',
      businessGlossary: [
        { term: 'bank-gateway', meaning: '银行侧网关' },
        { term: 'acquire_result', meaning: '收单返回码' }
      ],
      focusAreas: ['timeout', 'retry storm', 'downstream dependency failure'],
      riskPreference: ['涉及资金安全的问题，风险等级从严判断。'],
      outputPreference: ['summary 要短，root_cause 要明确点名服务链路。']
    }
  },
  {
    id: 2,
    name: '基础通用分析',
    content: {
      domainGoal: '通用分布式链路异常分析',
      businessGlossary: [],
      focusAreas: ['error propagation', 'timeout', 'abnormal latency'],
      riskPreference: ['证据不足时宁可返回 unknown，也不要过度推测。'],
      outputPreference: ['solution 要偏操作建议，不要写空泛结论。']
    }
  },
  {
    id: 3,
    name: '风控链路排障',
    content: {
      domainGoal: '风控链路排障',
      businessGlossary: [
        { term: 'feature pull', meaning: '特征拉取链路' },
        { term: 'rule hit', meaning: '规则命中判断' }
      ],
      focusAreas: ['cache miss', 'feature pull timeout', 'downstream dependency failure'],
      riskPreference: ['涉及误拒或漏放的链路问题，需要比普通性能抖动更谨慎。'],
      outputPreference: ['root_cause 要区分风控规则层和基础设施层。']
    }
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

const selectedPromptPreview = computed(() => {
  if (!selectedPrompt.value) return ''
  return renderBusinessGuidancePreview(selectedPrompt.value.content)
})

const promptEditorLabels = computed(() => {
  const isZh = general.language === 'zh'
  return {
    domainGoal: isZh ? '目标领域' : 'Domain Goal',
    businessGlossary: isZh ? '业务术语表' : 'Business Glossary',
    focusAreas: isZh ? '关注点' : 'Focus Areas',
    riskPreference: isZh ? '风险偏好' : 'Risk Preference',
    outputPreference: isZh ? '输出偏好' : 'Output Preference',
    preview: isZh ? '最终 Business Guidance 预览' : 'Rendered Business Guidance Preview',
    setActive: isZh ? '设为生效配置' : 'Set As Active Prompt',
    activeHint: isZh ? '保存后重启生效' : 'Takes effect after save and restart'
  }
})

const activePromptBadgeLabel = computed(() => {
  return general.language === 'zh' ? '生效中' : 'Active'
})

const selectedChannel = computed(() => {
  return channels.find((item) => item.id === selectedChannelId.value) ?? channels[0]
})

function toBool(value: unknown, defaultValue = false) {
  if (value === undefined || value === null) return defaultValue
  if (typeof value === 'boolean') return value
  if (typeof value === 'number') return value !== 0
  if (typeof value === 'string') return value === '1' || value.toLowerCase() === 'true'
  return defaultValue
}

function toNumber(value: unknown, defaultValue: number) {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'string' && value.trim() !== '') {
    const parsed = Number(value)
    if (Number.isFinite(parsed)) return parsed
  }
  return defaultValue
}

function createEmptyPromptContent(): PromptContentDraft {
  return {
    domainGoal: '',
    businessGlossary: [{ term: '', meaning: '' }],
    focusAreas: [''],
    riskPreference: [''],
    outputPreference: ['']
  }
}

function normalizePromptList(rawList: unknown) {
  if (!Array.isArray(rawList)) return []
  return rawList
    .filter((item): item is string => typeof item === 'string')
    .map((item) => item.trim())
    .filter((item) => item.length > 0)
}

function normalizePromptGlossary(rawGlossary: unknown) {
  if (!Array.isArray(rawGlossary)) return []
  return rawGlossary
    .map((item) => {
      if (!item || typeof item !== 'object') return null
      const row = item as Record<string, unknown>
      const term = typeof row.term === 'string' ? row.term.trim() : ''
      const meaning = typeof row.meaning === 'string' ? row.meaning.trim() : ''
      if (!term && !meaning) return null
      return { term, meaning }
    })
    .filter((item): item is PromptGlossaryDraft => item !== null)
}

// Prompt 编辑器这一步已经改成结构化表单，所以回填时先尽量按 JSON 结构解释。
// 如果旧库里还是自由文本，就先兜底塞进 domainGoal，至少保证原内容不会在原型页里直接丢失。
function parsePromptContent(value: unknown): PromptContentDraft {
  if (typeof value !== 'string' || value.trim() === '') {
    return createEmptyPromptContent()
  }

  try {
    const parsed = JSON.parse(value) as Record<string, unknown>
    const content: PromptContentDraft = {
      domainGoal: typeof parsed.domain_goal === 'string' ? parsed.domain_goal : '',
      businessGlossary: normalizePromptGlossary(parsed.business_glossary),
      focusAreas: normalizePromptList(parsed.focus_areas),
      riskPreference: normalizePromptList(parsed.risk_preference),
      outputPreference: normalizePromptList(parsed.output_preference)
    }

    if (content.businessGlossary.length === 0) content.businessGlossary.push({ term: '', meaning: '' })
    if (content.focusAreas.length === 0) content.focusAreas.push('')
    if (content.riskPreference.length === 0) content.riskPreference.push('')
    if (content.outputPreference.length === 0) content.outputPreference.push('')
    return content
  } catch {
    return {
      domainGoal: value.trim(),
      businessGlossary: [{ term: '', meaning: '' }],
      focusAreas: [''],
      riskPreference: [''],
      outputPreference: ['']
    }
  }
}

function serializePromptContent(content: PromptContentDraft) {
  const normalized = {
    domain_goal: content.domainGoal.trim(),
    business_glossary: normalizePromptGlossary(content.businessGlossary),
    focus_areas: normalizePromptList(content.focusAreas),
    risk_preference: normalizePromptList(content.riskPreference),
    output_preference: normalizePromptList(content.outputPreference)
  }
  return JSON.stringify(normalized)
}

// 业务 prompt 的编辑器按字段形态拆开了，但模型最终吃的仍然是一段 business_guidance。
// 所以这里保留只读预览，让用户能看见“结构化输入最后会渲染成什么样”，避免配置时心里没底。
function renderBusinessGuidancePreview(content: PromptContentDraft) {
  const lines: string[] = []
  const domainGoal = content.domainGoal.trim()
  if (domainGoal) {
    lines.push('Domain goal:')
    lines.push(domainGoal)
    lines.push('')
  }

  const glossary = normalizePromptGlossary(content.businessGlossary)
  if (glossary.length > 0) {
    lines.push('Business glossary:')
    glossary.forEach((item) => lines.push(`- ${item.term}: ${item.meaning}`))
    lines.push('')
  }

  const focusAreas = normalizePromptList(content.focusAreas)
  if (focusAreas.length > 0) {
    lines.push('Focus areas:')
    focusAreas.forEach((item) => lines.push(`- ${item}`))
    lines.push('')
  }

  const riskPreference = normalizePromptList(content.riskPreference)
  if (riskPreference.length > 0) {
    lines.push('Risk preference:')
    riskPreference.forEach((item) => lines.push(`- ${item}`))
    lines.push('')
  }

  const outputPreference = normalizePromptList(content.outputPreference)
  if (outputPreference.length > 0) {
    lines.push('Output preference:')
    outputPreference.forEach((item) => lines.push(`- ${item}`))
  }

  return lines.join('\n').trim()
}

// 后端现在会把别名作为数组回填到快照里，但这里仍保留字符串兼容分支。
// 原因是 Settings 控制面目前还是走 /settings/config 的标量接口，保存时会临时 stringify；
// 所以这个解析函数需要同时兼容“新后端数组回填”和“旧值/半迁移字符串”两种形态。
function parseAliasList(value: unknown) {
  if (Array.isArray(value)) {
    return value.filter((item): item is string => typeof item === 'string')
  }
  if (typeof value !== 'string' || value.trim() === '') {
    return []
  }
  try {
    const parsed = JSON.parse(value)
    if (!Array.isArray(parsed)) return []
    return parsed.filter((item): item is string => typeof item === 'string')
  } catch {
    return []
  }
}

function normalizeAliasDrafts(rawAliases: string[], primaryField: string) {
  const next: string[] = []
  const seen = new Set<string>()
  const normalizedPrimary = primaryField.trim()
  // 重复别名、空值、以及和主字段撞名的项都直接在前端控件状态里收掉。
  // 这一轮我们明确把“别名去重”放在 Settings 页面自己处理，后端只保留最低限度的语义过滤。
  for (const rawAlias of rawAliases) {
    const alias = rawAlias.trim()
    if (!alias) continue
    if (normalizedPrimary && alias === normalizedPrimary) continue
    if (seen.has(alias)) continue
    seen.add(alias)
    next.push(alias)
  }
  return next
}

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
 * 现在虽然已经接上后端，但这层本地快照仍然有价值：
 * 一是用来做 dirty check，二是用来判断哪些字段属于“保存后重启生效”。
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

function normalizePromptSelection() {
  if (prompts.length === 0) {
    ai.activePromptId = 0
    selectedPromptId.value = 0
    return
  }

  if (!prompts.some((item) => item.id === ai.activePromptId)) {
    ai.activePromptId = prompts[0].id
  }

  if (!prompts.some((item) => item.id === selectedPromptId.value)) {
    selectedPromptId.value = ai.activePromptId
  }
}

function applySnapshot(next: PrototypeSnapshot) {
  Object.assign(general, next.general)
  Object.assign(ai, next.ai)
  prompts.splice(0, prompts.length, ...next.prompts)
  channels.splice(0, channels.length, ...next.channels)
  Object.assign(kernel, next.kernel)
  kernel.endFlagAliases = normalizeAliasDrafts(kernel.endFlagAliases, kernel.endFlagField)
  normalizePromptSelection()
  ensureChannelSelection()
}

watch(
  [() => kernel.endFlagAliases.slice(), () => kernel.endFlagField],
  ([aliases, primaryField]) => {
    const normalized = normalizeAliasDrafts(aliases, primaryField)
    const unchanged = normalized.length === kernel.endFlagAliases.length
      && normalized.every((item, index) => item === kernel.endFlagAliases[index])
    if (unchanged) {
      return
    }
    kernel.endFlagAliases.splice(0, kernel.endFlagAliases.length, ...normalized)
  }
)

async function loadSettings() {
  isLoading.value = true
  try {
    const response = await fetch('/api/settings/all')
    if (!response.ok) {
      throw new Error('Failed to fetch settings')
    }

    const data: BackendSettingsResponse = await response.json()
    const config = data.config ?? {}
    const nextSnapshot: PrototypeSnapshot = {
      general: {
        language: (typeof config.app_language === 'string' ? config.app_language : 'zh'),
        httpPort: toNumber(config.http_port, 8080),
        retentionDays: toNumber(config.log_retention_days, 7)
      },
      ai: {
        analysisEnabled: toBool(config.ai_analysis_enabled, true),
        provider: typeof config.ai_provider === 'string' ? config.ai_provider : 'mock',
        model: typeof config.ai_model === 'string' ? config.ai_model : 'gpt-4-turbo',
        apiKey: typeof config.ai_api_key === 'string' ? config.ai_api_key : '',
        language: typeof config.ai_language === 'string' ? config.ai_language : 'zh',
        retryEnabled: toBool(config.ai_retry_enabled, false),
        retryMaxAttempts: toNumber(config.ai_retry_max_attempts, 3),
        autoDegrade: toBool(config.ai_auto_degrade, false),
        fallbackProvider: typeof config.ai_fallback_provider === 'string' ? config.ai_fallback_provider : 'mock',
        fallbackModel: typeof config.ai_fallback_model === 'string' ? config.ai_fallback_model : 'mock',
        // 这里单独回填 fallbackApiKey，是为了让“降级链路有独立凭证”这个语义在页面上可见、可改、可保存。
        fallbackApiKey: typeof config.ai_fallback_api_key === 'string' ? config.ai_fallback_api_key : '',
        circuitBreaker: toBool(config.ai_circuit_breaker, true),
        failureThreshold: toNumber(config.ai_failure_threshold, 5),
        cooldownSeconds: toNumber(config.ai_cooldown_seconds, 60),
        activePromptId: toNumber(config.active_prompt_id, 0)
      },
      prompts: (data.prompts ?? []).map((item) => ({
        id: item.id,
        name: item.name,
        content: parsePromptContent(item.content)
      })),
      channels: (data.channels ?? []).map((item) => ({
        id: item.id,
        name: item.name,
        enabled: toBool(item.is_active, false),
        webhookUrl: item.webhook_url,
        secret: item.secret ?? '',
        threshold: item.alert_threshold
      })),
      kernel: {
        workerThreads: toNumber(config.kernel_worker_threads, 4),
        endFlagField: typeof config.trace_end_field === 'string' ? config.trace_end_field : 'trace_end',
        endFlagAliases: parseAliasList(config.trace_end_aliases),
        tokenLimit: toNumber(config.token_limit, 0),
        spanCapacity: toNumber(config.span_capacity, 100),
        collectingIdleMs: toNumber(config.collecting_idle_timeout_ms, 5000),
        sealedGraceMs: toNumber(config.sealed_grace_window_ms, 1000),
        retryBaseDelayMs: toNumber(config.retry_base_delay_ms, 500),
        sweepTickMs: toNumber(config.sweep_tick_ms, 500),
        watermarks: {
          activeSessions: {
            overload: toNumber(config.wm_active_sessions_overload, 75),
            critical: toNumber(config.wm_active_sessions_critical, 90)
          },
          bufferedSpans: {
            overload: toNumber(config.wm_buffered_spans_overload, 75),
            critical: toNumber(config.wm_buffered_spans_critical, 90)
          },
          pendingTasks: {
            overload: toNumber(config.wm_pending_tasks_overload, 75),
            critical: toNumber(config.wm_pending_tasks_critical, 90)
          }
        }
      }
    }

    if (nextSnapshot.prompts.length === 0) {
      nextSnapshot.prompts.push({
        id: 1,
        name: '默认 Prompt',
        content: createEmptyPromptContent()
      })
    }
    if (nextSnapshot.channels.length === 0) {
      nextSnapshot.channels.push({
        id: 1,
        name: '默认飞书渠道',
        enabled: false,
        webhookUrl: '',
        secret: '',
        threshold: 'critical'
      })
    }

    if (!nextSnapshot.prompts.some((item) => item.id === nextSnapshot.ai.activePromptId)) {
      nextSnapshot.ai.activePromptId = nextSnapshot.prompts[0].id
    }

    applySnapshot(nextSnapshot)
    savedSnapshot.value = snapshotState()
  } catch (error) {
    console.error('Failed to load settings prototype data:', error)
    ElMessage.error('设置加载失败，当前先保留本地默认值')
  } finally {
    isLoading.value = false
  }
}

function savePrototype() {
  void persistSettings()
}

function resetPrototype() {
  applySnapshot(JSON.parse(JSON.stringify(savedSnapshot.value)) as PrototypeSnapshot)
  ElMessage.info('已恢复到上一次保存的设置')
}

/**
 * 左侧点击现在只负责切换编辑对象。
 * active prompt 已经被明确收口成“冷启动配置”，所以不能再用一次点击同时完成“选中”和“设为生效”，
 * 否则用户会误以为当前运行中的分析逻辑已经立刻切过去了。
 */
function handlePromptClick(id: number) {
  selectedPromptId.value = id
}

// active prompt 现在必须通过独立按钮显式设置。
// 这样用户会先看右侧内容，再决定哪条在“保存并重启”后真正生效，语义比点击列表即切换更干净。
function setSelectedPromptActive() {
  if (!selectedPrompt.value) return
  ai.activePromptId = selectedPrompt.value.id
}

function addPrompt() {
  const nextId = Math.max(...prompts.map((item) => item.id), 0) + 1
  prompts.unshift({
    id: nextId,
    name: `新业务 Prompt ${nextId}`,
    content: createEmptyPromptContent()
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

function addPromptGlossaryRow() {
  selectedPrompt.value?.content.businessGlossary.push({ term: '', meaning: '' })
}

function removePromptGlossaryRow(index: number) {
  if (!selectedPrompt.value) return
  if (selectedPrompt.value.content.businessGlossary.length <= 1) return
  selectedPrompt.value.content.businessGlossary.splice(index, 1)
}

function addPromptListRow(target: 'focusAreas' | 'riskPreference' | 'outputPreference') {
  selectedPrompt.value?.content[target].push('')
}

function removePromptListRow(target: 'focusAreas' | 'riskPreference' | 'outputPreference', index: number) {
  if (!selectedPrompt.value) return
  if (selectedPrompt.value.content[target].length <= 1) return
  selectedPrompt.value.content[target].splice(index, 1)
}

/**
 * 测试发送和保存设置必须分开：
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

// 这里故意不复用旧 system.ts。
// 原因不是重复造轮子，而是旧 store 里还带着 maxDiskUsageGB / msg_template / ioBuffer 这些过时字段；
// 当前原型页要验证的是“新字段契约能不能真实落库并稳定回填”，所以直接按三类接口对接最干净。
async function persistSettings() {
  isSaving.value = true
  try {
    const restartTouched = restartRequired.value
    const configItems = [
      { key: 'app_language', value: general.language },
      { key: 'http_port', value: general.httpPort.toString() },
      { key: 'log_retention_days', value: general.retentionDays.toString() },
      { key: 'ai_analysis_enabled', value: ai.analysisEnabled ? '1' : '0' },
      { key: 'ai_provider', value: ai.provider },
      { key: 'ai_model', value: ai.model },
      { key: 'ai_api_key', value: ai.apiKey },
      { key: 'ai_language', value: ai.language },
      { key: 'ai_retry_enabled', value: ai.retryEnabled ? '1' : '0' },
      { key: 'ai_retry_max_attempts', value: ai.retryMaxAttempts.toString() },
      { key: 'ai_auto_degrade', value: ai.autoDegrade ? '1' : '0' },
      { key: 'ai_fallback_provider', value: ai.fallbackProvider },
      { key: 'ai_fallback_model', value: ai.fallbackModel },
      // 保存时把 fallback key 和 provider/model 一起发下去，避免后端只能看到半套降级配置。
      { key: 'ai_fallback_api_key', value: ai.fallbackApiKey },
      { key: 'ai_circuit_breaker', value: ai.circuitBreaker ? '1' : '0' },
      { key: 'ai_failure_threshold', value: ai.failureThreshold.toString() },
      { key: 'ai_cooldown_seconds', value: ai.cooldownSeconds.toString() },
      { key: 'active_prompt_id', value: ai.activePromptId.toString() },
      { key: 'kernel_worker_threads', value: kernel.workerThreads.toString() },
      { key: 'trace_end_field', value: kernel.endFlagField },
      // 这里暂时继续走字符串，是为了不把 /settings/config 这条标量接口一起推翻重写。
      // 真正的持久化已经不再存 JSON；Repository 收到这一项后会在写库边界解析一次，并改写到 trace_end_aliases 单独表里。
      { key: 'trace_end_aliases', value: JSON.stringify(kernel.endFlagAliases) },
      { key: 'token_limit', value: kernel.tokenLimit.toString() },
      { key: 'span_capacity', value: kernel.spanCapacity.toString() },
      { key: 'collecting_idle_timeout_ms', value: kernel.collectingIdleMs.toString() },
      { key: 'sealed_grace_window_ms', value: kernel.sealedGraceMs.toString() },
      { key: 'retry_base_delay_ms', value: kernel.retryBaseDelayMs.toString() },
      { key: 'sweep_tick_ms', value: kernel.sweepTickMs.toString() },
      { key: 'wm_active_sessions_overload', value: kernel.watermarks.activeSessions.overload.toString() },
      { key: 'wm_active_sessions_critical', value: kernel.watermarks.activeSessions.critical.toString() },
      { key: 'wm_buffered_spans_overload', value: kernel.watermarks.bufferedSpans.overload.toString() },
      { key: 'wm_buffered_spans_critical', value: kernel.watermarks.bufferedSpans.critical.toString() },
      { key: 'wm_pending_tasks_overload', value: kernel.watermarks.pendingTasks.overload.toString() },
      { key: 'wm_pending_tasks_critical', value: kernel.watermarks.pendingTasks.critical.toString() }
    ]

    const promptsPayload = prompts.map((item) => ({
      id: item.id,
      name: item.name,
      content: serializePromptContent(item.content),
      is_active: item.id === ai.activePromptId ? 1 : 0
    }))

    const channelsPayload = channels.map((item) => ({
      id: item.id,
      name: item.name,
      provider: 'feishu',
      webhook_url: item.webhookUrl,
      secret: item.secret,
      alert_threshold: item.threshold,
      is_active: item.enabled ? 1 : 0
    }))

    const responses = await Promise.all([
      fetch('/api/settings/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ items: configItems })
      }),
      fetch('/api/settings/prompts', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(promptsPayload)
      }),
      fetch('/api/settings/channels', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(channelsPayload)
      })
    ])

    const failed = responses.find((response) => !response.ok)
    if (failed) {
      throw new Error(`Save failed with status ${failed.status}`)
    }

    await loadSettings()
    ElMessage.success(restartTouched ? '设置已保存：包含重启后生效字段' : '设置已保存')
  } catch (error) {
    console.error('Failed to save settings prototype data:', error)
    ElMessage.error('设置保存失败')
  } finally {
    isSaving.value = false
  }
}

watch(() => general.language, (newLang) => {
  // @ts-ignore
  i18n.global.locale.value = newLang
}, { immediate: true })

onMounted(() => {
  void loadSettings()
})
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
