<template>
   <div class="h-full flex flex-col p-8 overflow-y-auto custom-scrollbar">
     <div class="max-w-6xl mx-auto w-full space-y-8">
       
       <!-- Header Area -->
       <div class="border-b border-gray-700 pb-4">
         <h2 class="text-2xl font-bold text-white tracking-wide">{{ $t('settings.title') }}</h2>
         <p class="text-gray-500 mt-1">{{ $t('settings.subtitle') }}</p>
       </div>
 
       <!-- Settings Form -->
       <el-form label-position="top" size="large" class="demo-tabs">
         <el-tabs type="border-card" class="custom-tabs">
 
           <!-- Group 0: General Settings -->
           <el-tab-pane :label="$t('settings.tabs.general')">
             <template #label>
               <span class="flex items-center gap-2">
                 <el-icon><Setting /></el-icon> {{ $t('settings.tabs.general') }}
               </span>
             </template>
 
             <div class="p-6 space-y-8">
                <!-- Language -->
                <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                   <el-form-item :label="$t('settings.general.language')">
                      <el-select v-model="systemStore.settings.general.language" class="w-full md:w-1/2">
                         <el-option label="English" value="en" />
                         <el-option label="中文 (Chinese)" value="zh" />
                      </el-select>
                   </el-form-item>
                </div>

                <!-- Log Retention Strategy -->
                <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                   <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">{{ $t('settings.general.logRetention') }}</h3>
                   <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
                      <el-form-item :label="$t('settings.general.retentionPeriod')">
                         <el-input-number v-model="systemStore.settings.general.logRetentionDays" :min="1" :max="365">
                            <template #suffix>{{ $t('settings.general.days') }}</template>
                         </el-input-number>
                      </el-form-item>
                      <el-form-item :label="$t('settings.general.maxDisk')">
                         <el-input-number v-model="systemStore.settings.general.maxDiskUsageGB" :min="1" :max="1000">
                            <template #suffix>GB</template>
                         </el-input-number>
                      </el-form-item>
                   </div>
                </div>

                <!-- Network Configuration -->
                <div class="bg-[#1a1a1a] border border-gray-700 p-6 rounded">
                   <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">{{ $t('settings.general.network') }}</h3>
                   <el-form-item :label="$t('settings.general.httpPort')">
                      <el-input-number v-model="systemStore.settings.general.httpPort" :min="1024" :max="65535" />
                   </el-form-item>
                </div>
             </div>
           </el-tab-pane>
           
           <!-- Group A: AI Pipeline (T-Shape Layout) -->
           <el-tab-pane :label="$t('settings.tabs.ai')">
             <template #label>
               <span class="flex items-center gap-2">
                 <el-icon><Cpu /></el-icon> {{ $t('settings.tabs.ai') }}
               </span>
             </template>
             
             <div class="flex flex-col h-[700px]">
               
               <!-- Region A: Top Global Engine Config -->
               <div class="bg-[#1a1a1a] border-b border-gray-700 p-6 flex flex-col justify-center shrink-0">
                  <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">{{ $t('settings.ai.globalTitle') }}</h3>
                  <div class="grid grid-cols-1 md:grid-cols-4 gap-6">
                     <el-form-item :label="$t('settings.ai.provider')">
                       <el-select v-model="systemStore.settings.ai.provider" class="w-full">
                         <el-option label="OpenAI (GPT-4o)" value="OpenAI" />
                         <el-option label="Google Gemini Pro" value="Gemini" />
                         <el-option label="Azure OpenAI Service" value="Azure" />
                         <el-option label="Local Mock (Dev)" value="Local-Mock" />
                       </el-select>
                     </el-form-item>
 
                     <el-form-item :label="$t('settings.ai.modelName')">
                        <el-input v-model="systemStore.settings.ai.modelName" placeholder="e.g. gpt-4-turbo" />
                     </el-form-item>
 
                     <el-form-item :label="$t('settings.ai.apiKey')">
                        <el-input 
                           v-model="systemStore.settings.ai.apiKey" 
                           type="password" 
                           show-password 
                           :placeholder="$t('settings.ai.apiKeyPlaceholder')" 
                         />
                     </el-form-item>
 
                     <el-form-item :label="$t('settings.ai.analysisLang')">
                        <el-select v-model="systemStore.settings.ai.language" class="w-full">
                           <el-option label="English" value="en" />
                           <el-option label="中文 (Chinese)" value="zh" />
                        </el-select>
                     </el-form-item>
                  </div>
               </div>

                <!-- Region B: Resilience & Reliability (New Module) -->
                <div class="bg-[#1a1a1a] border-b border-gray-700 p-6 flex flex-col justify-center shrink-0">
                   <h3 class="text-sm font-bold text-gray-400 uppercase mb-4">{{ $t('settings.ai.resilienceTitle') }}</h3>
                   <div class="grid grid-cols-1 md:grid-cols-2 gap-8">
                      <!-- Auto-Degradation -->
                      <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                         <div class="flex items-center justify-between mb-4">
                            <span class="text-sm font-bold text-gray-300">{{ $t('settings.ai.autoDegrade') }}</span>
                            <el-switch v-model="systemStore.settings.ai.autoDegrade" inline-prompt :active-text="$t('settings.ai.enableDegrade')" />
                         </div>
                         <el-form-item :label="$t('settings.ai.fallbackModel')" class="mb-0">
                            <el-input v-model="systemStore.settings.ai.fallbackModel" :disabled="!systemStore.settings.ai.autoDegrade" placeholder="e.g. local-mock" />
                         </el-form-item>
                      </div>

                      <!-- Circuit Breaker -->
                      <div class="border border-gray-700 p-4 rounded bg-gray-800/30">
                         <div class="flex items-center justify-between mb-4">
                            <span class="text-sm font-bold text-gray-300">{{ $t('settings.ai.circuitBreaker') }}</span>
                            <el-switch v-model="systemStore.settings.ai.circuitBreaker" inline-prompt :active-text="$t('settings.ai.enableBreaker')" />
                         </div>
                         <div class="flex gap-4">
                            <el-form-item :label="$t('settings.ai.failureThreshold')" class="flex-1 mb-0">
                               <el-input-number v-model="systemStore.settings.ai.failureThreshold" :min="1" :max="100" class="w-full" />
                            </el-form-item>
                            <el-form-item :label="$t('settings.ai.cooldown')" class="flex-1 mb-0">
                               <el-input-number v-model="systemStore.settings.ai.cooldownSeconds" :min="10" :max="3600" class="w-full">
                                  <template #suffix>s</template>
                               </el-input-number>
                            </el-form-item>
                         </div>
                      </div>
                   </div>
                </div>

                <!-- Region C: Bottom Strategy Manager -->
               <div class="flex-1 flex flex-col md:flex-row border-t border-gray-700 min-h-0">
                  <!-- Left: Prompt List -->
                  <div class="w-full md:w-[25%] border-r border-gray-700 flex flex-col bg-[#1a1a1a]">
                     <div class="p-4 border-b border-gray-700 flex justify-between items-center bg-[#252525]">
                        <span class="text-sm font-bold text-gray-400 uppercase">{{ $t('settings.ai.promptList') }}</span>
                        <el-button type="primary" size="small" circle @click="addNewPrompt">
                          <el-icon><Plus /></el-icon>
                        </el-button>
                     </div>
                     <div class="flex-1 overflow-y-auto custom-scrollbar">
                        <div 
                           v-for="(prompt, index) in systemStore.settings.ai.prompts" 
                           :key="prompt.id"
                           class="p-4 cursor-pointer hover:bg-gray-800 transition-colors border-b border-gray-800 relative group flex items-center"
                           :class="{'bg-gray-800 border-l-2 border-l-primary': selectedPromptId === prompt.id}"
                           @click="handlePromptClick(prompt.id)"
                        >
                           <!-- Status Dot -->
                           <div class="mr-3 flex items-center justify-center">
                              <div v-if="prompt.is_active" class="w-2.5 h-2.5 rounded-full bg-green-500 shadow-[0_0_8px_rgba(34,197,94,0.6)]"></div>
                              <div v-else class="w-2.5 h-2.5 rounded-full border border-gray-500"></div>
                           </div>

                           <div class="font-mono text-sm text-gray-300 truncate flex-1 pr-2">{{ prompt.name }}</div>
                           
                           <!-- Delete Button -->
                           <div 
                             class="opacity-0 group-hover:opacity-100 transition-opacity ml-auto"
                             v-if="systemStore.settings.ai.prompts.length > 1"
                           >
                              <el-button type="danger" link size="small" @click.stop="deletePrompt(index)">
                                <el-icon><Delete /></el-icon>
                              </el-button>
                           </div>
                        </div>
                     </div>
                  </div>
 
                  <!-- Right: Strategy Editor -->
                  <div class="w-full md:w-[75%] p-6 flex flex-col bg-[#2d2d2d]">
                     <div v-if="selectedPrompt" class="flex flex-col h-full space-y-4">
                        <el-form-item :label="$t('settings.ai.promptName')">
                           <el-input v-model="selectedPrompt.name" />
                        </el-form-item>
                        
                        <el-form-item :label="$t('settings.ai.templateContent')" class="flex-1 flex flex-col custom-form-item-full">
                           <el-input
                             type="textarea"
                             v-model="selectedPrompt.content"
                             class="font-mono text-xs custom-textarea h-full"
                             resize="none"
                           />
                        </el-form-item>
                     </div>
                     <div v-else class="h-full flex items-center justify-center text-gray-500">
                        Select a prompt to edit
                     </div>
                  </div>
               </div>
 
             </div>
           </el-tab-pane>
 
           <!-- Group B: Integration (Master-Detail) -->
           <el-tab-pane :label="$t('settings.tabs.integration')">
             <template #label>
                <span class="flex items-center gap-2">
                 <el-icon><Share /></el-icon> {{ $t('settings.tabs.integration') }}
               </span>
             </template>
             
             <div class="flex flex-col md:flex-row h-[600px] border-t border-gray-700">
                <!-- Left: Channel List -->
                <div class="w-full md:w-[30%] border-r border-gray-700 flex flex-col bg-[#1a1a1a]">
                   <div class="p-4 border-b border-gray-700 flex justify-between items-center">
                      <span class="text-sm font-bold text-gray-400 uppercase">{{ $t('settings.integration.channels') }}</span>
                      <el-button type="primary" size="small" circle @click="addNewChannel">
                        <el-icon><Plus /></el-icon>
                      </el-button>
                   </div>
                   <div class="flex-1 overflow-y-auto custom-scrollbar">
                      <div 
                         v-for="(channel, index) in systemStore.settings.integration.channels"
                         :key="channel.id"
                         class="p-4 cursor-pointer hover:bg-gray-800 transition-colors border-b border-gray-800 flex justify-between items-center group"
                         :class="{'bg-gray-800 border-l-2 border-l-primary': selectedChannelId === channel.id}"
                         @click="selectedChannelId = channel.id"
                      >
                         <div class="flex items-center gap-2 truncate">
                            <div class="w-2 h-2 rounded-full" :class="channel.enabled ? 'bg-green-500' : 'bg-gray-600'"></div>
                            <span class="font-mono text-sm text-gray-300 truncate">{{ channel.name }}</span>
                         </div>
                         <div class="flex items-center gap-2">
                             <el-switch v-model="channel.enabled" size="small" @click.stop />
                             <!-- Delete Button -->
                             <el-button type="danger" link size="small" @click.stop="deleteChannel(index)" class="opacity-0 group-hover:opacity-100 transition-opacity">
                                <el-icon><Delete /></el-icon>
                             </el-button>
                         </div>
                      </div>
                   </div>
                </div>
 
                <!-- Right: Detail -->
                <div class="w-full md:w-[70%] p-6 flex flex-col bg-[#2d2d2d]">
                   <div v-if="selectedChannel" class="space-y-6">
                      <div class="grid grid-cols-2 gap-6">
                         <el-form-item :label="$t('settings.integration.vendor')">
                            <el-select v-model="selectedChannel.vendor" class="w-full" @change="onVendorChange(selectedChannel)">
                               <el-option label="DingTalk (钉钉)" value="DingTalk" />
                               <el-option label="Lark / Feishu (飞书)" value="Lark" />
                               <el-option label="Slack" value="Slack" />
                               <el-option label="Custom (Webhook)" value="Custom" />
                            </el-select>
                         </el-form-item>
 
                         <el-form-item :label="$t('settings.integration.webhookName')">
                            <el-input v-model="selectedChannel.name" />
                         </el-form-item>
                      </div>
 
                      <el-form-item :label="$t('settings.integration.webhookUrl')">
                         <el-input v-model="selectedChannel.url" placeholder="https://..." >
                            <template #prepend>POST</template>
                         </el-input>
                      </el-form-item>
                      
                      <!-- New Alert Threshold Control -->
                      <el-form-item :label="$t('settings.integration.threshold')">
                          <div class="w-full flex items-center gap-4">
                              <el-radio-group v-model="selectedChannel.threshold" class="custom-radio-group" :class="thresholdClass(selectedChannel.threshold)">
                                 <el-radio-button label="Critical">{{ $t('settings.integration.critical') }}</el-radio-button>
                                 <el-radio-button label="Error">{{ $t('settings.integration.error') }}</el-radio-button>
                                 <el-radio-button label="Warning">{{ $t('settings.integration.warning') }}</el-radio-button>
                              </el-radio-group>
                              <div class="text-xs text-gray-500 italic flex-1">
                                  {{ $t('settings.integration.hint') }}
                              </div>
                          </div>
                      </el-form-item>
 
                      <el-form-item :label="$t('settings.integration.payloadTemplate')">
                         <el-input 
                            type="textarea" 
                            v-model="selectedChannel.template" 
                            :rows="8" 
                            class="font-mono text-xs custom-textarea"
                         />
                      </el-form-item>
                      
                      <div class="pt-4 flex justify-end">
                         <el-button type="success" plain @click="sendTestMessage">
                            <el-icon class="mr-2"><Promotion /></el-icon> {{ $t('settings.integration.testMessage') }}
                         </el-button>
                      </div>
                   </div>
                   <div v-else class="h-full flex items-center justify-center text-gray-500">
                      Select a channel to configure
                   </div>
                </div>
             </div>
           </el-tab-pane>
 
           <!-- Group C: Kernel (Refactored) -->
           <el-tab-pane :label="$t('settings.tabs.kernel')">
             <template #label>
                <span class="flex items-center gap-2">
                 <el-icon><Operation /></el-icon> {{ $t('settings.tabs.kernel') }}
               </span>
             </template>
             <div class="p-4 grid grid-cols-1 md:grid-cols-2 gap-8">
                <!-- Adaptive Micro-batching Switch -->
                <el-form-item class="col-span-1 md:col-span-2">
                  <div class="flex items-center justify-between border border-gray-700 p-4 rounded bg-gray-800/50 w-full">
                     <div>
                       <span class="block text-sm font-bold" :class="systemStore.settings.kernel.adaptiveBatching ? 'text-primary' : 'text-gray-300'">
                         {{ systemStore.settings.kernel.adaptiveBatching ? $t('settings.kernel.adaptiveLabel') : $t('settings.kernel.fixedLabel') }}
                       </span>
                       <span class="text-xs text-gray-500">
                         {{ systemStore.settings.kernel.adaptiveBatching ? $t('settings.ai.adaptiveDesc') : $t('settings.ai.fixedDesc') }}
                       </span>
                     </div>
                     <el-switch 
                         v-model="systemStore.settings.kernel.adaptiveBatching" 
                         size="large"
                         inline-prompt
                         :active-text="$t('settings.kernel.adaptiveMode')"
                         inactive-text="Fixed"
                     />
                  </div>
                </el-form-item>
 
                <el-form-item :label="systemStore.settings.kernel.adaptiveBatching ? $t('settings.ai.adaptiveBatch') : $t('settings.ai.fixedBatch')">
                  <div class="w-full px-2">
                    <el-slider 
                       v-model="systemStore.settings.ai.maxBatchSize" 
                       :min="10" 
                       :max="500" 
                       show-input
                    />
                  </div>
                </el-form-item>
 
                <el-form-item :label="$t('settings.kernel.flushInterval')">
                   <el-input-number v-model="systemStore.settings.kernel.flushInterval" :min="10" :max="5000" :step="50">
                      <template #suffix>ms</template>
                   </el-input-number>
                   <div class="text-xs text-gray-500 mt-1">{{ $t('settings.kernel.flushDesc') }}</div>
                </el-form-item>
 
                <el-form-item :label="$t('settings.kernel.worker')">
                  <el-input-number v-model="systemStore.settings.kernel.workerThreads" :min="1" :max="32" />
                </el-form-item>
 
                <el-form-item :label="$t('settings.kernel.ioBuffer')">
                  <el-select v-model="systemStore.settings.kernel.ioBufferSize" class="w-full">
                    <el-option label="128MB" value="128MB" />
                    <el-option label="256MB" value="256MB" />
                    <el-option label="512MB" value="512MB" />
                    <el-option label="1GB" value="1GB" />
                  </el-select>
                </el-form-item>
             </div>
           </el-tab-pane>
         </el-tabs>
       </el-form>
 
       <!-- Save Button Area -->
       <div class="flex justify-end pt-4 border-t border-gray-700">
         <el-button
            type="primary"
            size="large"
            @click="handleSave"
            :disabled="!systemStore.isDirty"
         >
           <el-icon class="mr-2"><Check /></el-icon> {{ $t('settings.save') }}
         </el-button>
       </div>
 
     </div>
   </div>
 </template>
 
 <script setup lang="ts">
 import { ref, computed, onMounted } from 'vue'
 import { useSystemStore, type WebhookConfig } from '../stores/system'
 import { Cpu, Share, Operation, Check, Plus, Delete, Promotion, Setting } from '@element-plus/icons-vue'
 import { ElMessage } from 'element-plus'
 import { useI18n } from 'vue-i18n'
 
 const systemStore = useSystemStore()
 const { t } = useI18n()
 
 // Fetch settings on mount
 onMounted(() => {
    systemStore.fetchSettings()
 })
 
 // --- AI Pipeline Logic ---
 const selectedPromptId = ref<string | number | undefined>(undefined)
 
 // Initialize selection once prompts are loaded
 const selectedPrompt = computed(() => {
    if (!selectedPromptId.value && systemStore.settings.ai.prompts.length > 0) {
       selectedPromptId.value = systemStore.settings.ai.prompts[0].id
    }
    return systemStore.settings.ai.prompts.find(p => p.id === selectedPromptId.value)
 })
 
 function addNewPrompt() {
    const newId = 'p' + Date.now()
    systemStore.settings.ai.prompts.push({
       id: newId,
       name: 'New Prompt',
       content: '',
       is_active: 0
    })
    selectedPromptId.value = newId
 }

 function deletePrompt(index: number) {
    const deletedId = systemStore.settings.ai.prompts[index].id
    
    systemStore.settings.ai.prompts.splice(index, 1)
    
    // If active prompt was deleted (checked by ID), make the first one active if exists
    // Note: Use weak comparison (==) to handle string/number mismatch
    if (systemStore.settings.ai.activePromptId == deletedId && systemStore.settings.ai.prompts.length > 0) {
        const nextPrompt = systemStore.settings.ai.prompts[0]
        // Only set activePromptId if the ID is a valid number (persisted prompt)
        // If it's a new prompt (string ID), we can't set it as persisted active ID yet,
        // but we can update the UI selection.
        if (typeof nextPrompt.id === 'number') {
           systemStore.settings.ai.activePromptId = nextPrompt.id
        }
    }

    if (selectedPromptId.value === deletedId && systemStore.settings.ai.prompts.length > 0) {
       selectedPromptId.value = systemStore.settings.ai.prompts[0].id
    }
 }

 function handlePromptClick(id: string | number) {
    // 1. Select for editing
    selectedPromptId.value = id
    
    // 2. Set as Active (Mutex logic)
    // Only set activePromptId if the ID is a valid number (persisted prompt)
    if (typeof id === 'number') {
        systemStore.settings.ai.activePromptId = id
    }

    // Legacy support: keep is_active flag for now if needed, or just relying on ID match
    systemStore.settings.ai.prompts.forEach(p => {
        p.is_active = (p.id === id) ? 1 : 0
    })
 }
 
 // --- Integration Logic ---
 const selectedChannelId = ref<string | number | undefined>(undefined)
 
 // Initialize selection once channels are loaded
 const selectedChannel = computed(() => {
    if (!selectedChannelId.value && systemStore.settings.integration.channels.length > 0) {
       selectedChannelId.value = systemStore.settings.integration.channels[0].id
    }
    return systemStore.settings.integration.channels.find(c => c.id === selectedChannelId.value)
 })
 
 function addNewChannel() {
    const newId = 'c' + Date.now()
    systemStore.settings.integration.channels.push({
       id: newId,
       name: 'New Channel',
       vendor: 'Custom',
       url: '',
       threshold: 'Error',
       template: '{}',
       enabled: false
    })
    selectedChannelId.value = newId
 }

 function deleteChannel(index: number) {
    const deletedId = systemStore.settings.integration.channels[index].id
    systemStore.settings.integration.channels.splice(index, 1)

    // Update selection if the deleted one was selected
    if (selectedChannelId.value === deletedId) {
        if (systemStore.settings.integration.channels.length > 0) {
             selectedChannelId.value = systemStore.settings.integration.channels[0].id
        } else {
             selectedChannelId.value = undefined
        }
    }
 }
 
 function onVendorChange(channel: WebhookConfig) {
    switch(channel.vendor) {
       case 'DingTalk':
          channel.template = '{"msgtype": "text", "text": {"content": "Alert: {{summary}}"}}'
          break
       case 'Slack':
          channel.template = '{"text": "Alert: {{summary}}"}'
          break
       case 'Lark':
          channel.template = '{"msg_type": "text", "content": {"text": "Alert: {{summary}}"}}'
          break
       default:
          channel.template = '{}'
    }
 }
 
 function thresholdClass(level: string) {
     if (level === 'Critical') return 'is-critical'
     if (level === 'Error') return 'is-error'
     if (level === 'Warning') return 'is-warning'
     return ''
 }
 
 function sendTestMessage() {
    ElMessage.success(t('settings.integration.testSuccess'))
 }
 
 // --- Save ---
 const handleSave = async () => {
    try {
       await systemStore.saveSettings()
       // Success message is handled in store (optional, but store has better context for restart logic)
       // If store doesn't handle success message for general save, we can add it here too,
       // but the store implementation I wrote handles it.
    } catch (e) {
       // Error handled in store
    }
 }
 </script>
 
 <style scoped>
 /* Customizing Element Plus Tabs for Dark Mode "Cyberpunk" feel */
 :deep(.el-tabs--border-card) {
   background-color: transparent;
   border-color: #374151; /* gray-700 */
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
   color: #9ca3af; /* gray-400 */
 }
 
 :deep(.el-tabs--border-card > .el-tabs__content) {
   background-color: #2d2d2d;
   padding: 0;
 }
 
 /* Custom Textarea look for Detail panes */
 :deep(.custom-textarea .el-textarea__inner) {
   background-color: #1a1a1a !important;
   color: #a8a29e !important; /* warm gray */
   border-color: #444 !important;
   padding-top: 12px;
   font-family: 'Fira Code', monospace;
   font-size: 0.8rem;
   height: 100%;
 }
 
 :deep(.custom-form-item-full .el-form-item__content) {
    height: 100%;
 }
 
 /* Dynamic styling for threshold radio group */
 :deep(.custom-radio-group.is-critical .el-radio-button__original-radio:checked + .el-radio-button__inner) {
     background-color: #ef4444 !important; /* red-500 */
     border-color: #ef4444 !important;
     box-shadow: -1px 0 0 0 #ef4444 !important;
 }
 
 :deep(.custom-radio-group.is-error .el-radio-button__original-radio:checked + .el-radio-button__inner) {
     background-color: #f97316 !important; /* orange-500 */
     border-color: #f97316 !important;
     box-shadow: -1px 0 0 0 #f97316 !important;
 }
 
 :deep(.custom-radio-group.is-warning .el-radio-button__original-radio:checked + .el-radio-button__inner) {
     background-color: #eab308 !important; /* yellow-500 */
     border-color: #eab308 !important;
     box-shadow: -1px 0 0 0 #eab308 !important;
     color: #1f2937 !important; /* dark text for yellow */
 }
 
 .custom-scrollbar::-webkit-scrollbar {
   width: 8px;
 }
 .custom-scrollbar::-webkit-scrollbar-thumb {
   background: #4b5563;
   border-radius: 4px;
 }
 </style>
 