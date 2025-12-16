import { defineStore } from 'pinia'
import { ref, reactive } from 'vue'
import dayjs from 'dayjs'
import { ElMessage } from 'element-plus'

export interface LogEntry {
  id: number | string
  timestamp: string
  level: 'INFO' | 'WARN' | 'RISK'
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
  id: string
  name: string
  content: string
}

export interface WebhookConfig {
  id: string
  name: string
  vendor: 'DingTalk' | 'Lark' | 'Slack' | 'Custom'
  url: string
  threshold: 'Critical' | 'Error' | 'Warning'
  template: string
  enabled: boolean
}

export interface AISettings {
    provider: 'OpenAI' | 'Gemini' | 'Local-Mock' | 'Azure'
    modelName: string
    apiKey: string
    language: 'en' | 'zh'
    maxBatchSize: number
    prompts: PromptConfig[]
}

// Interface for Backend API Responses
export interface AlertInfoResponse {
    trace_id: string
    summary: string
    time: string
}

export interface DashboardStatsResponse {
    total_logs: number
    high_risk: number
    medium_risk: number
    low_risk: number
    info_risk: number
    unknown_risk: number
    avg_response_time: number
    recent_alerts: AlertInfoResponse[]
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

export const useSystemStore = defineStore('system', () => {
  // --- State ---
  const isRunning = ref(false)
  const isSimulationMode = ref(true) // Default to true (safe mode)
  const pollingInterval = ref<number | null>(null)

  // Metrics
  const totalLogsProcessed = ref(1245890) // Default to previous mock value
  const netLatency = ref(0.05) // ms
  const aiLatency = ref(800) // ms
  const aiTriggerCount = ref(8420)
  const memoryUsage = ref<number[]>([]) // Array for history
  const memoryPercent = ref(68)
  const queuePercent = ref(12)
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

  // Logs
  const logs = ref<LogEntry[]>([])

  // Settings
  const settings = reactive({
    ai: {
      provider: 'Local-Mock',
      modelName: 'gpt-4-turbo',
      apiKey: '',
      language: 'en',
      maxBatchSize: 50,
      prompts: [
        {
          id: 'p1',
          name: 'Security Audit',
          content: `Analyze the following log entries for potential security breaches. 
Focus on: SQL Injection patterns, XSS attempts, and unauthorized access.
Output format: JSON with "risk_score" and "analysis".

Logs:
{{logs_batch}}`
        },
        {
          id: 'p2',
          name: 'Root Cause Analysis',
          content: `Identify the root cause of the system failure based on the provided stack traces and error logs.
Correlate timestamps between services.

Context:
{{logs_batch}}`
        },
        {
          id: 'p3',
          name: 'Performance Tuning',
          content: `Review the latency metrics and DB query logs. 
Suggest indexing strategies or caching layers where appropriate.

Metrics:
{{metrics_batch}}`
        }
      ]
    } as AISettings,
    integration: {
      alertThreshold: 'High', // Legacy field, kept for safety but moved to per-channel
      channels: [
        {
          id: 'c1',
          name: 'Ops Team (DingTalk)',
          vendor: 'DingTalk',
          url: 'https://oapi.dingtalk.com/robot/send?access_token=...',
          threshold: 'Error',
          template: '{"msgtype": "text", "text": {"content": "Alert: {{summary}}"}}',
          enabled: true
        },
        {
          id: 'c2',
          name: 'Security (Slack)',
          vendor: 'Slack',
          url: 'https://hooks.slack.com/services/...',
          threshold: 'Critical',
          template: '{"text": "Alert: {{summary}}"}',
          enabled: false
        }
      ] as WebhookConfig[]
    },
    kernel: {
      workerThreads: 4,
      ioBufferSize: '256MB',
      sqliteWalSync: true,
      adaptiveBatching: true,
      flushInterval: 200
    }
  })

  // --- Constants for Mocking ---
  const LOG_MESSAGES = {
    INFO: [
      'Received batch of events from ingress-nginx',
      'Processing chunk ID #49281',
      'Flushing buffer to disk...',
      'AI analysis completed for request #8821',
      'Health check passed: component=db-connector'
    ],
    WARN: [
      'High latency detected in ingress (150ms)',
      'Buffer usage > 60%, scaling consumers',
      'Retry attempt 1/3 for downstream service',
      'Memory fragment warning in worker #2'
    ],
    RISK: [
      'SQL Injection attempt detected from IP 192.168.1.104',
      'Anomaly detection triggered: Unusual payload size',
      'Unauthorized access attempt on /admin/config',
      'Cross-Site Scripting (XSS) signature match'
    ]
  }

  const LOG_MESSAGES_ZH = {
    INFO: [
      '从 ingress-nginx 接收到一批事件',
      '正在处理分块 ID #49281',
      '正在将缓冲区刷新到磁盘...',
      '请求 #8821 的 AI 分析已完成',
      '健康检查通过: component=db-connector'
    ],
    WARN: [
      '检测到 Ingress 高延迟 (150ms)',
      '缓冲区使用率 > 60%，正在扩展消费者',
      '下游服务重试尝试 1/3',
      '工作线程 #2 中的内存碎片警告'
    ],
    RISK: [
      '检测到来自 IP 192.168.1.104 的 SQL 注入尝试',
      '触发异常检测：异常的负载大小',
      '对 /admin/config 的未授权访问尝试',
      '跨站脚本 (XSS) 签名匹配'
    ]
  }

  // --- Mock Actions ---

  function generateRandomLog() {
    const r = Math.random()
    let level: 'INFO' | 'WARN' | 'RISK' = 'INFO'
    if (r > 0.96) level = 'RISK'
    else if (r > 0.85) level = 'WARN'

    const messageSet = settings.ai.language === 'zh' ? LOG_MESSAGES_ZH : LOG_MESSAGES
    // @ts-ignore
    const messages = messageSet[level]
    const message = messages[Math.floor(Math.random() * messages.length)] || 'Unknown system event'

    logs.value.push({
      id: Date.now() + Math.random(),
      timestamp: dayjs().format('HH:mm:ss.SSS'),
      level,
      message
    })

    if (logs.value.length > 200) {
      logs.value.shift()
    }
  }

  function updateMockData() {
    // 1. Total Logs
    const inc = Math.floor(Math.random() * 50) + 10
    totalLogsProcessed.value += inc

    // 2. AI Count
    if (Math.random() > 0.5) {
      aiTriggerCount.value += Math.floor(Math.random() * 5)
    }

    // 3. Backpressure & Queue
    if (Math.random() > 0.95) {
       // Occasional spikes
       queuePercent.value = Math.min(100, queuePercent.value + Math.floor(Math.random() * 15))
    } else {
       // Drain
       queuePercent.value = Math.max(5, queuePercent.value - Math.floor(Math.random() * 5))
    }

    if (queuePercent.value > 80) backpressureStatus.value = 'Full'
    else if (queuePercent.value > 50) backpressureStatus.value = 'Active'
    else backpressureStatus.value = 'Normal'

    // 4. Memory & Latency
    const newMem = Math.floor(Math.random() * 30) + 40 // 40-70%
    memoryPercent.value = newMem
    memoryUsage.value.push(newMem)
    if (memoryUsage.value.length > 20) memoryUsage.value.shift()

    // Latency jitter
    netLatency.value = Math.max(0.01, +(netLatency.value + (Math.random() - 0.5) * 0.05).toFixed(3))
    aiLatency.value = Math.max(100, Math.floor(aiLatency.value + (Math.random() - 0.5) * 100))

    // Risk Stats Jitter (Rarely add to counts)
    if (Math.random() > 0.8) riskStats.Safe += Math.floor(Math.random() * 10)
    if (Math.random() > 0.98) riskStats.Info += 1
    if (Math.random() > 0.99) riskStats.Warning += 1
    if (Math.random() > 0.995) {
        riskStats.Error += 1
        // Add alert
        recentAlerts.value.unshift({
            id: Date.now(),
            time: dayjs().format('HH:mm:ss'),
            service: ['DB-Connector', 'Ingress', 'AI-Worker'][Math.floor(Math.random() * 3)],
            level: 'Error',
            summary: 'Simulated system error event detected'
        })
        if (recentAlerts.value.length > 7) recentAlerts.value.pop()
    }

    // 5. Chart Data
    const now = dayjs().format('HH:mm:ss')
    const qps = Math.floor(Math.random() * 1000) + 500
    const aiRate = Math.floor(qps * (Math.random() * 0.2 + 0.1)) // 10-30% of QPS

    chartData.value.push({
      time: now,
      qps,
      aiRate
    })
    if (chartData.value.length > 60) chartData.value.shift()
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
        
        const data: DashboardStatsResponse = await res.json();
        
        // Update State
        totalLogsProcessed.value = data.total_logs;
        netLatency.value = data.avg_response_time;
        
        riskStats.Critical = data.high_risk;
        riskStats.Error = data.medium_risk;
        riskStats.Warning = data.low_risk;
        riskStats.Info = data.info_risk;
        riskStats.Safe = data.total_logs - (data.high_risk + data.medium_risk + data.low_risk + data.info_risk);

        recentAlerts.value = data.recent_alerts.map(a => ({
            id: a.trace_id,
            time: a.time,
            service: 'LogSentinel',
            level: 'Error',
            summary: a.summary
        }));

        // Simulate missing data for visual completeness until backend supports it
        const now = dayjs().format('HH:mm:ss')
        const qps = Math.floor(Math.random() * 100 + 50); // Mock QPS
        const aiRate = Math.floor(qps * 0.1);
        chartData.value.push({ time: now, qps, aiRate });
        if (chartData.value.length > 60) chartData.value.shift();

        memoryPercent.value = 45 + Math.floor(Math.random() * 5); // Mock 45-50%

    } catch (e) {
        if (isSimulationMode.value) {
            console.warn("Dashboard fetch failed, falling back to mock data:", e);
            updateMockData();
        } else {
            console.error("Dashboard fetch failed:", e);
            showBackendError();
        }
    }
  }

  async function fetchLogs() {
      try {
          const res = await fetch('/api/history?page=1&pageSize=50');
          if (!res.ok) throw new Error('Failed to fetch logs');
          const data: HistoryPageResponse = await res.json();

          logs.value = data.logs.map(item => {
              let level: 'INFO' | 'WARN' | 'RISK' = 'INFO';
              if (item.risk_level === 'Critical' || item.risk_level === 'High') level = 'RISK';
              else if (item.risk_level === 'Warning' || item.risk_level === 'Medium') level = 'WARN';
              
              return {
                  id: item.trace_id,
                  timestamp: item.processed_at,
                  level: level,
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

  function startPolling() {
    if (pollingInterval.value) return
    
    // Initial data population if empty (for chart)
    if (chartData.value.length === 0) {
        for (let i = 0; i < 60; i++) {
             chartData.value.push({
                time: dayjs().subtract(60-i, 'second').format('HH:mm:ss'),
                qps: 0,
                aiRate: 0
             })
        }
    }

    // Initial fetch
    fetchDashboardStats();
    fetchLogs();

    // Poll every 1s
    // @ts-ignore
    pollingInterval.value = setInterval(() => {
        fetchDashboardStats();
        fetchLogs();
    }, 1000) 
  }

  function stopPolling() {
    if (pollingInterval.value) {
      clearInterval(pollingInterval.value)
      pollingInterval.value = null
    }
  }

  return {
    isRunning,
    isSimulationMode,
    settings,
    totalLogsProcessed,
    netLatency,
    aiLatency,
    aiTriggerCount,
    backpressureStatus,
    queuePercent,
    memoryUsage,
    memoryPercent,
    chartData,
    riskStats,
    recentAlerts,
    logs,
    toggleSystem
  }
})
