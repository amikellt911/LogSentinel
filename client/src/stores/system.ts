import { defineStore } from 'pinia'
import { ref, reactive, computed, watch } from 'vue'
import dayjs from 'dayjs'
import { ElMessage, ElMessageBox } from 'element-plus'
import _ from 'lodash'
import i18n from '../i18n'
import { formatToBeijingTime } from '../utils/timeFormat'

export interface LogEntry {
  id: number | string
  timestamp: string
  level: 'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe' | 'Unknown'
  trace_id: string
  span_id: string
  message: string
}

export interface MetricPoint {
  time: string
  qps: number
  aiRate: number
}

export interface AlertEntry {
  id: number | string
  time: string
  service: string
  level: 'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe'
  summary: string
}

export interface PromptConfig {
  id: string | number
  name: string
  content: string
  is_active?: number | boolean
}

export interface WebhookConfig {
  id: string | number
  name: string
  vendor: 'dingtalk' | 'lark' | 'slack' | 'custom'
  url: string
  threshold: 'critical' | 'error' | 'warning'
  template: string
  enabled: boolean
}

export interface AISettings {
    provider: 'openai' | 'gemini' | 'mock' | 'azure'
    modelName: string
    apiKey: string
    language: 'en' | 'zh'
    maxBatchSize: number
    autoDegrade: boolean
    fallbackModel: string
    circuitBreaker: boolean
    failureThreshold: number
    cooldownSeconds: number
    activePromptId: number
    prompts: PromptConfig[]
}

export interface GeneralSettings {
    language: 'en' | 'zh'
    logRetentionDays: number
    maxDiskUsageGB: number
    httpPort: number
}

// Interface for Backend API Responses
export interface AlertInfoResponse {
    trace_id: string
    summary: string
    time: string
}

export interface SystemRuntimeOverviewResponse {
    total_logs: number
    ai_call_total: number
    ai_queue_wait_ms: number
    ai_inference_latency_ms: number
    memory_rss_mb: number
    backpressure_status: 'Normal' | 'Active' | 'Full'
}

export interface SystemTokenStatsResponse {
    input_tokens: number
    output_tokens: number
    total_tokens: number
    avg_tokens_per_call: number
}

export interface SystemMetricPointResponse {
    time_ms: number
    ingest_rate: number
    ai_completion_rate: number
}

export interface SystemRuntimeSnapshotResponse {
    overview: SystemRuntimeOverviewResponse
    token_stats: SystemTokenStatsResponse
    timeseries: SystemMetricPointResponse[]
}

export interface HistoricalLogItemResponse {
    trace_id: string
    risk_level: string // "Critical", "Error", etc.
    summary: string
    processed_at: string
}

export interface HistoryPageResponse {
    logs: HistoricalLogItemResponse[]
    total_count: number
}

// API Response Interfaces for Settings
export interface SettingsResponse {
  config: Record<string, any>
  prompts: {
    id: number
    name: string
    content: string
    is_active: number | boolean
  }[]
  channels: {
    id: number
    name: string
    provider: string
    webhook_url: string
    alert_threshold: string
    msg_template: string
    is_active: number
  }[]
}

export const useSystemStore = defineStore('system', () => {
  // --- State ---
  const isRunning = ref(false)
  const isSimulationMode = ref(true) // Default to true (safe mode)
  const pollingInterval = ref<number | null>(null)

  // Metrics
  const totalLogsProcessed = ref(0)
  const aiQueueWaitMs = ref(0)
  const aiInferenceLatencyMs = ref(0)
  const aiCallTotal = ref(0)
  const inputTokens = ref(0)
  const outputTokens = ref(0)
  const totalTokens = ref(0)
  const avgTokensPerCall = ref(0)
  const memoryRssMb = ref(0)
  const backpressureStatus = ref<'Normal' | 'Active' | 'Full'>('Normal')
  
  const chartData = ref<MetricPoint[]>([])

  const riskStats = reactive({
    Critical: 2,
    Error: 15,
    Warning: 45,
    Info: 120,
    Safe: 8500
  })

  const recentAlerts = ref<AlertEntry[]>([
    { id: 1, time: '10:23:01', service: 'Auth-Service', level: 'Critical', summary: 'SQL Injection pattern detected in login payload' },
    { id: 2, time: '10:23:05', service: 'Ingress-Gateway', level: 'Warning', summary: 'High latency on route /api/v1/data' },
    { id: 3, time: '10:24:12', service: 'DB-Connector', level: 'Error', summary: 'Connection pool exhausted (retry 2/3)' }
  ])

  const latestBatchSummary = ref<string>("System initialized. Waiting for log batches...")

  // Logs
  const logs = ref<LogEntry[]>([])

  // Settings State
  const settings = reactive({
    general: {
      language: 'en',
      logRetentionDays: 7,
      maxDiskUsageGB: 1,
      httpPort: 8080
    } as GeneralSettings,
    ai: {
      provider: 'mock',
      modelName: 'gpt-4-turbo',
      apiKey: '',
      language: 'en',
      maxBatchSize: 50,
      autoDegrade: false,
      fallbackModel: 'mock',
      circuitBreaker: true,
      failureThreshold: 5,
      cooldownSeconds: 60,
      activePromptId: 0,
      prompts: [
        {
          id: 'p-aggregate',
          name: 'Microservice Trace Summary',
          content: `You are an observability assistant. Given aggregated logs from the same trace_id and ordered span_id, identify the main issue, impacted service, and suggested action.`,
          is_active: 1
        }
      ]
    } as AISettings,
    integration: {
      alertThreshold: 'High', // Legacy field, kept for safety but moved to per-channel
      channels: [
        {
          id: 'c1',
          name: 'Ops Team (DingTalk)',
          vendor: 'dingtalk',
          url: 'https://oapi.dingtalk.com/robot/send?access_token=...',
          threshold: 'error',
          template: '{"msgtype": "text", "text": {"content": "Alert: {{summary}}"}}',
          enabled: true
        }
      ] as WebhookConfig[]
    },
    kernel: {
      workerThreads: 4,
      ioBufferSize: '256MB',
      adaptiveBatching: true,
      flushInterval: 200
    }
  })

  // Synced State (Deep Copy)
  // Initialize with a deep copy of the default settings
  const syncedSettings = ref(_.cloneDeep(settings))

  // Computed Property for Dirty Check
  const isDirty = computed(() => {
      return !_.isEqual(settings, syncedSettings.value)
  })

  // Watch for language changes and update i18n immediately
  watch(() => settings.general.language, (newLang) => {
    // @ts-ignore
    i18n.global.locale.value = newLang
  })

  // Cold Config Keys that require restart
  // const REQUIRE_RESTART_KEYS = ['kernel_worker_threads', 'kernel_io_buffer'] // Unused for now as we hardcode the check

  // --- Constants for Mocking ---
  const LOG_MESSAGES = {
    Info: [
      'Received batch of events from ingress-nginx',
      'Processing chunk ID #49281',
      'Flushing buffer to disk...',
      'AI analysis completed for request #8821',
      'Health check passed: component=db-connector'
    ],
    Safe: [
      'Routine operation completed',
      'All systems nominal',
      'Regular maintenance task finished'
    ],
    Warning: [
      'High latency detected in ingress (150ms)',
      'Buffer usage > 60%, scaling consumers',
      'Retry attempt 1/3 for downstream service',
      'Memory fragment warning in worker #2'
    ],
    Error: [
      'Connection timeout to database',
      'Service temporarily unavailable',
      'Failed to process request: invalid format'
    ],
    Critical: [
      'SQL Injection attempt detected from IP 192.168.1.104',
      'Anomaly detection triggered: Unusual payload size',
      'Unauthorized access attempt on /admin/config',
      'Cross-Site Scripting (XSS) signature match'
    ]
  }

  const LOG_MESSAGES_ZH = {
    Info: [
      '从 ingress-nginx 接收到一批事件',
      '正在处理分块 ID #49281',
      '正在将缓冲区刷新到磁盘...',
      '请求 #8821 的 AI 分析已完成',
      '健康检查通过: component=db-connector'
    ],
    Safe: [
      '常规操作已完成',
      '所有系统运行正常',
      '定期维护任务已完成'
    ],
    Warning: [
      '检测到 Ingress 高延迟 (150ms)',
      '缓冲区使用率 > 60%，正在扩展消费者',
      '下游服务重试尝试 1/3',
      '工作线程 #2 中的内存碎片警告'
    ],
    Error: [
      '数据库连接超时',
      '服务暂时不可用',
      '处理请求失败：格式无效'
    ],
    Critical: [
      '检测到来自 IP 192.168.1.104 的 SQL 注入尝试',
      '异常检测触发：异常载荷大小',
      '未授权访问尝试 /admin/config',
      '跨站脚本 (XSS) 签名匹配'
    ]
  }

  // --- Mock Actions ---

  function generateRandomLog() {
    const r = Math.random()
    let level: 'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe' | 'Unknown' = 'Info'
    if (r > 0.96) level = 'Critical'
    else if (r > 0.90) level = 'Error'
    else if (r > 0.85) level = 'Warning'
    else if (r > 0.70) level = 'Safe'

    const messageSet = settings.ai.language === 'zh' ? LOG_MESSAGES_ZH : LOG_MESSAGES
    // @ts-ignore
    const messages = messageSet[level]
    const message = messages[Math.floor(Math.random() * messages.length)] || 'Unknown system event'

    // 生成 trace_id 和 span_id
    const traceNum = Math.floor(Math.random() * 9000) + 1000
    const spanNum = Math.floor(Math.random() * 900) + 100
    const trace_id = `trace_${traceNum}`
    const span_id = `span_${spanNum}`

    logs.value.push({
      id: Date.now() + Math.random(),
      timestamp: dayjs().format('HH:mm:ss.SSS'),
      level,
      trace_id,
      span_id,
      message
    })

    if (logs.value.length > 200) {
      logs.value.shift()
    }
  }

  // --- Actions ---

  function toggleSystem(value: boolean) {
    isRunning.value = value
    if (value) {
      startPolling()
    } else {
      stopPolling()
    }
  }

  // Throttle error messages to avoid spamming
  let lastErrorTime = 0;
  function showBackendError() {
      const now = Date.now();
      if (now - lastErrorTime > 3000) { // Only show every 3 seconds
          ElMessage.error('Backend connection failed. System paused or unreachable.');
          lastErrorTime = now;
      }
  }

  async function fetchDashboardStats() {
    try {
        const res = await fetch('/api/dashboard', { method: 'GET' }); 
        
        if (!res.ok) throw new Error('Failed to fetch dashboard stats');
        
        const data: SystemRuntimeSnapshotResponse = await res.json();

        // Dashboard 现在已经切到后端系统运行态快照，不再沿用旧的 dashboard mock/风险分布结构。
        // 这里直接按 overview/token_stats/timeseries 三段映射，避免前端再自己拼假数据。
        totalLogsProcessed.value = data.overview.total_logs;
        aiQueueWaitMs.value = data.overview.ai_queue_wait_ms;
        aiInferenceLatencyMs.value = data.overview.ai_inference_latency_ms;
        aiCallTotal.value = data.overview.ai_call_total;
        memoryRssMb.value = data.overview.memory_rss_mb;
        backpressureStatus.value = data.overview.backpressure_status;

        inputTokens.value = data.token_stats.input_tokens;
        outputTokens.value = data.token_stats.output_tokens;
        totalTokens.value = data.token_stats.total_tokens;
        avgTokensPerCall.value = data.token_stats.avg_tokens_per_call;

        // 后端 timeseries 的 time_ms 当前来自内部采样时钟，不是给用户直接看的墙上时间。
        // 所以前端这里只借用“点的顺序”回填展示标签，保证折线图连续且不把 steady_clock 数字直接暴露到界面上。
        const now = dayjs();
        chartData.value = data.timeseries.map((point, index, all) => ({
          time: now.subtract(all.length - 1 - index, 'second').format('HH:mm:ss'),
          qps: point.ingest_rate,
          aiRate: point.ai_completion_rate
        }));

    } catch (e) {
        console.error("Dashboard fetch failed:", e);
        showBackendError();
    }
  }

  async function fetchLogs() {
      try {
          const res = await fetch('/api/history?page=1&pageSize=50');
          if (!res.ok) throw new Error('Failed to fetch logs');
          const data: HistoryPageResponse = await res.json();

          logs.value = data.logs.map(item => {
              // 使用和 History.vue 相同的等级映射逻辑
              let level = item.risk_level;
              const lower = level.toLowerCase();
              if (lower === 'critical' || lower === 'high') level = 'Critical';
              else if (lower === 'error' || lower === 'medium') level = 'Error';
              else if (lower === 'warning' || lower === 'low') level = 'Warning';
              else if (lower === 'info') level = 'Info';
              else if (lower === 'safe') level = 'Safe';
              else if (lower === 'unknown') level = 'Unknown';

              return {
                  id: item.trace_id,
                  timestamp: formatToBeijingTime(item.processed_at),
                  level: level as any,
                  message: item.summary
              };
          });

      } catch (e) {
          if (isSimulationMode.value) {
              console.warn("Logs fetch failed, falling back to mock data:", e);
              generateRandomLog();
              generateRandomLog();
          } else {
             console.error("Logs fetch failed:", e);
             // Error already shown by dashboard fetch usually, so maybe skip or show unique error
          }
      }
  }

  // --- Settings API Actions ---

  async function fetchSettings() {
    try {
      const res = await fetch('/api/settings/all')
      if (!res.ok) throw new Error('Failed to fetch settings')
      const data: SettingsResponse = await res.json()

      // Helper to parse boolean from various backend types (string "1"/"true", number 1, boolean true)
      const toBool = (val: any, defaultVal: boolean = false) => {
        if (val === undefined || val === null) return defaultVal;
        if (typeof val === 'boolean') return val;
        if (typeof val === 'number') return val !== 0;
        if (typeof val === 'string') return val === '1' || val.toLowerCase() === 'true';
        return defaultVal;
      };

      // Map Backend Data to Store State
      const newSettings = {
        general: {
          language: (data.config['app_language'] || 'en') as any,
          logRetentionDays: parseInt(data.config['log_retention_days'] || '7'),
          maxDiskUsageGB: parseInt(data.config['max_disk_usage_gb'] || '1'),
          httpPort: parseInt(data.config['http_port'] || '8080')
        },
        ai: {
          provider: ((data.config['ai_provider'] || 'mock') as string).toLowerCase() as any,
          modelName: data.config['ai_model'] || 'gpt-4-turbo',
          apiKey: data.config['ai_api_key'] || '',
          language: (data.config['ai_language'] || 'en') as any,
          maxBatchSize: parseInt(data.config['kernel_max_batch'] || '50'),
          autoDegrade: toBool(data.config['ai_auto_degrade'], false),
          fallbackModel: data.config['ai_fallback_model'] || 'mock',
          circuitBreaker: toBool(data.config['ai_circuit_breaker'], true), // Default true
          failureThreshold: parseInt(data.config['ai_failure_threshold'] || '5'),
          cooldownSeconds: parseInt(data.config['ai_cooldown_seconds'] || '60'),

          activePromptId: parseInt(data.config['active_prompt_id'] || '0'),

          prompts: data.prompts.map(p => ({
            id: p.id,
            name: p.name,
            content: p.content,
            is_active: toBool(p.is_active, false) // Map backend is_active
          }))
        },
        integration: {
          alertThreshold: 'High', // Placeholder
          channels: data.channels.map(c => ({
            id: c.id,
            name: c.name,
            vendor: ((c.provider || 'custom') as string).toLowerCase() as any,
            url: c.webhook_url,
            threshold: ((c.alert_threshold || 'error') as string).toLowerCase() as any,
            template: c.msg_template,
            enabled: !!c.is_active
          }))
        },
        kernel: {
          workerThreads: parseInt(data.config['kernel_worker_threads'] || '4'),
          ioBufferSize: data.config['kernel_io_buffer'] || '256MB',
          adaptiveBatching: toBool(data.config['kernel_adaptive_mode'], true),
          flushInterval: parseInt(data.config['kernel_refresh_interval'] || '200')
        }
      }

      // Update State and Synced Copy
      Object.assign(settings, newSettings)
      syncedSettings.value = _.cloneDeep(newSettings)

    } catch (e) {
      console.warn("Settings fetch failed (simulation mode maybe?):", e)
      // In simulation mode, we just stick to default values
    }
  }

  async function saveSettings() {
    try {
       const promises = []

       // 1. Config Batch
       const configItems = [
         { key: 'app_language', value: settings.general.language },
         { key: 'log_retention_days', value: settings.general.logRetentionDays.toString() },
         { key: 'max_disk_usage_gb', value: settings.general.maxDiskUsageGB.toString() },
         { key: 'http_port', value: settings.general.httpPort.toString() },
         
         { key: 'ai_provider', value: settings.ai.provider },
         { key: 'ai_model', value: settings.ai.modelName },
         { key: 'ai_api_key', value: settings.ai.apiKey },
         { key: 'ai_language', value: settings.ai.language },
         { key: 'ai_auto_degrade', value: settings.ai.autoDegrade ? '1' : '0' },
         { key: 'ai_fallback_model', value: settings.ai.fallbackModel },
         { key: 'ai_circuit_breaker', value: settings.ai.circuitBreaker ? '1' : '0' },
         { key: 'ai_failure_threshold', value: settings.ai.failureThreshold.toString() },
         { key: 'ai_cooldown_seconds', value: settings.ai.cooldownSeconds.toString() },

         { key: 'active_prompt_id', value: settings.ai.activePromptId.toString() },

         { key: 'kernel_adaptive_mode', value: settings.kernel.adaptiveBatching ? '1' : '0' },
         { key: 'kernel_max_batch', value: settings.ai.maxBatchSize.toString() },
         { key: 'kernel_refresh_interval', value: settings.kernel.flushInterval.toString() },
         { key: 'kernel_worker_threads', value: settings.kernel.workerThreads.toString() },
         { key: 'kernel_io_buffer', value: settings.kernel.ioBufferSize }
       ]

       // Only send if config changed (comparing with synced is hard because synced structure is nested object, not KV list)
       // We can rely on basic dirty check: if ANY part of settings changed, we might as well send config if we are lazy.
       // But let's try to be precise if we can.
       // Actually, we can just always send config if isDirty is true, OR compare specific sub-trees.
       // Given the request emphasizes batch and robustness, sending all changed sections is good.
       // Let's compare `settings.ai` (minus prompts) and `settings.kernel` with `syncedSettings`.

       const isConfigDirty = !_.isEqual(_.omit(settings.ai, 'prompts'), _.omit(syncedSettings.value.ai, 'prompts')) ||
                             !_.isEqual(settings.kernel, syncedSettings.value.kernel) ||
                             !_.isEqual(settings.general, syncedSettings.value.general)

       if (isConfigDirty) {
          promises.push(fetch('/api/settings/config', {
             method: 'POST',
             headers: { 'Content-Type': 'application/json' },
             body: JSON.stringify({ items: configItems })
           }))
       }

       // 2. Prompts Batch
       if (!_.isEqual(settings.ai.prompts, syncedSettings.value.ai.prompts)) {
           const promptsPayload = settings.ai.prompts.map(p => ({
               id: typeof p.id === 'number' ? p.id : 0, // 0 for new
               name: p.name,
               content: p.content,
               is_active: p.is_active ? 1 : 0
           }))
           promises.push(fetch('/api/settings/prompts', {
               method: 'POST',
               headers: { 'Content-Type': 'application/json' },
               body: JSON.stringify(promptsPayload)
           }))
       }

       // 3. Channels Batch
       if (!_.isEqual(settings.integration.channels, syncedSettings.value.integration.channels)) {
           const channelsPayload = settings.integration.channels.map(c => ({
               id: typeof c.id === 'number' ? c.id : 0, // 0 for new
               name: c.name,
               provider: c.vendor,
               webhook_url: c.url,
               alert_threshold: c.threshold,
               msg_template: c.template,
               is_active: c.enabled ? 1 : 0
           }))
           promises.push(fetch('/api/settings/channels', {
               method: 'POST',
               headers: { 'Content-Type': 'application/json' },
               body: JSON.stringify(channelsPayload)
           }))
       }

       // Execute all needed requests
       await Promise.all(promises)

       // 4. Update Synced State via Re-fetch (to get new IDs)
       // We only re-fetch if we actually sent something.
       if (promises.length > 0) {
           await fetchSettings()
       } else {
           // Should not happen if isDirty is true, but just in case
           syncedSettings.value = _.cloneDeep(settings)
       }

       return true // Success
    } catch (e) {
       console.error("Save failed", e)
       throw e
    }
  }

  // Wrapped Save Action with Logic
  async function saveSettingsWithLogic() {
      // Check for Restart Needed
      let restartNeeded = false

      // Compare specific keys
      if (settings.kernel.workerThreads !== syncedSettings.value.kernel.workerThreads) restartNeeded = true
      if (settings.kernel.ioBufferSize !== syncedSettings.value.kernel.ioBufferSize) restartNeeded = true

      try {
          await saveSettings()

          ElMessage.success('Settings saved successfully')

          if (restartNeeded) {
              ElMessageBox.confirm(
                'Core configuration changed. Restart required to apply changes.',
                'Restart Required',
                {
                  confirmButtonText: 'Restart Now',
                  cancelButtonText: 'Later',
                  type: 'warning',
                }
              ).then(() => {
                 fetch('/api/restart', { method: 'POST' }).then(() => {
                     ElMessage.info('System is restarting...')
                     setTimeout(() => window.location.reload(), 3000)
                 })
              }).catch(() => {
                  ElMessage.info('Changes saved. Please restart manually later.')
              })
          }

      } catch (e) {
          ElMessage.error('Failed to save settings')
      }
  }


  function startPolling() {
    if (pollingInterval.value) return

    // Initial fetch
    fetchDashboardStats();
    // fetchLogs(); // Moved to explicit call by consumers (e.g. LiveLogs)

    // Poll every 1s
    // @ts-ignore
    pollingInterval.value = setInterval(() => {
        fetchDashboardStats();
        // fetchLogs(); // Disabled global log polling to prevent conflicts with History view
    }, 1000) 
  }

  function stopPolling() {
    if (pollingInterval.value) {
      clearInterval(pollingInterval.value)
      pollingInterval.value = null
    }
  }

  // Explicit log polling actions
  const logPollingInterval = ref<number | null>(null)
  
  function startLogPolling() {
      if (logPollingInterval.value) return
      fetchLogs()
      // @ts-ignore
      logPollingInterval.value = setInterval(() => {
          fetchLogs()
      }, 1000)
  }

  function stopLogPolling() {
      if (logPollingInterval.value) {
          clearInterval(logPollingInterval.value)
          logPollingInterval.value = null
      }
  }

  return {
    isRunning,
    isSimulationMode,
    settings,
    syncedSettings, // Exposed for debugging if needed
    isDirty,
    totalLogsProcessed,
    aiQueueWaitMs,
    aiInferenceLatencyMs,
    aiCallTotal,
    inputTokens,
    outputTokens,
    totalTokens,
    avgTokensPerCall,
    backpressureStatus,
    memoryRssMb,
    chartData,
    riskStats,
    recentAlerts,
    latestBatchSummary,
    logs,
    toggleSystem,
    fetchSettings,
    saveSettings: saveSettingsWithLogic,
    startLogPolling,
    stopLogPolling
  }
})
