import { createI18n } from 'vue-i18n'

const messages = {
  en: {
    layout: {
      dashboard: 'Dashboard',
      logs: 'Live Logs',
      settings: 'Settings',
      systemRunning: 'SYSTEM RUNNING',
      systemIdle: 'SYSTEM IDLE',
      missionControl: 'Mission Control Dashboard',
      eventStream: 'Real-time Event Stream',
      configuration: 'System Configuration'
    },
    dashboard: {
      totalLogs: 'Total Logs Processed',
      aiTrigger: 'AI Trigger Count',
      backpressure: 'Back-pressure Status',
      memory: 'Memory Usage',
      normal: 'Normal',
      active: 'Active',
      full: 'Full',
      stable: 'System Stable',
      throttling: 'Throttling Active',
      congested: 'Queue Congested',
      ingestRate: 'Ingest Rate (QPS)',
      aiRate: 'AI Rate',
      realtimeTitle: 'Real-time Ingestion vs AI Processing',
      calls: 'calls',
      netLatency: 'Net Latency (C++ Ingest)',
      aiLatency: 'AI Latency (Inference)',
      queue: 'Queue',
      riskTitle: 'Risk Distribution',
      recentAlerts: 'Recent Alerts',
      table: {
        time: 'Time',
        service: 'Service',
        level: 'Level',
        summary: 'AI Analysis Summary'
      }
    },
    logs: {
      receiving: 'RECEIVING STREAM...',
      paused: 'STREAM PAUSED',
      buffered: 'Events Buffered'
    },
    settings: {
      title: 'System Configuration',
      subtitle: 'Manage global policies, AI pipelines, and kernel parameters.',
      tabs: {
        general: 'General',
        ai: 'AI Pipeline',
        integration: 'Integration',
        kernel: 'Kernel'
      },
      general: {
        language: 'Application Language'
      },
      ai: {
        globalTitle: 'Global Engine Config',
        provider: 'AI Provider',
        modelName: 'Model Name',
        apiKey: 'API Key',
        apiKeyPlaceholder: 'Required for non-mock providers',
        analysisLang: 'Parse Language',
        maxBatch: 'Max Batch Size',
          adaptiveBatch: 'Adaptive Max Batch',
          fixedBatch: 'Fixed Batch Size',
          adaptiveDesc: 'Algorithm dynamically adjusts batch size based on back-pressure.',
          fixedDesc: 'System waits for buffer to fill before processing.',
        promptTemplate: 'Prompt Template',
          preview: 'PREVIEW',
          promptList: 'Prompts',
          newPrompt: 'New Prompt',
          promptName: 'Prompt Name',
          templateContent: 'Template Content'
      },
      integration: {
        threshold: 'Alert Threshold',
          critical: 'Critical',
          error: 'Error',
          warning: 'Warning',
          hint: 'High: Only critical security risks. Low: All warnings and above.',
          channels: 'Notification Channels',
          newChannel: 'New Channel',
          vendor: 'Vendor',
          webhookName: 'Webhook Name',
          webhookUrl: 'Webhook URL',
          payloadTemplate: 'Payload Template (JSON)',
          testMessage: 'Send Test Message',
          testSuccess: 'Test message sent successfully!'
      },
      kernel: {
        worker: 'Worker Threads',
        ioBuffer: 'IO Buffer Size',
        wal: 'SQLite WAL Sync',
        walTitle: 'Write-Ahead Logging (WAL)',
          walDesc: 'Enable synchronous checkpointing for data durability.',
          adaptiveMode: 'Adaptive Micro-batching',
          adaptiveLabel: 'Adaptive Mode',
          fixedLabel: 'Fixed Mode',
          flushInterval: 'Flush Interval',
          flushDesc: 'Force flush every N ms to prevent starvation.'
      },
      save: 'Save Configuration',
      success: 'Configuration applied successfully'
    }
  },
  zh: {
    layout: {
      dashboard: '仪表盘',
      logs: '实时日志',
      settings: '系统设置',
      systemRunning: '系统运行中',
      systemIdle: '系统待机',
      missionControl: '任务控制仪表盘',
      eventStream: '实时事件流',
      configuration: '系统配置'
    },
    dashboard: {
      totalLogs: '日志处理总数',
      aiTrigger: 'AI 触发次数',
      backpressure: '背压状态',
      memory: '内存占用',
      normal: '正常',
      active: '激活',
      full: '满载',
      stable: '系统稳定',
      throttling: '限流中',
      congested: '队列拥堵',
      ingestRate: '摄入速率 (QPS)',
      aiRate: 'AI 处理率',
      realtimeTitle: '实时摄入 vs AI 处理',
      calls: '次调用',
      netLatency: '网络延迟 (C++ 接收)',
      aiLatency: 'AI 延迟 (模型推理)',
      queue: '队列',
      riskTitle: '风险分布',
      recentAlerts: '最近告警',
      table: {
        time: '时间',
        service: '服务',
        level: '级别',
        summary: 'AI 分析摘要'
      }
    },
    logs: {
      receiving: '正在接收数据流...',
      paused: '数据流已暂停',
      buffered: '个缓存事件'
    },
    settings: {
      title: '系统配置',
      subtitle: '管理全局策略、AI 管道和内核参数。',
      tabs: {
        general: '常规',
        ai: 'AI 管道',
        integration: '集成',
        kernel: '内核'
      },
      general: {
        language: '系统语言'
      },
      ai: {
        globalTitle: '全局引擎配置',
        provider: 'AI 提供商',
        modelName: '模型名称',
        apiKey: 'API 密钥',
        apiKeyPlaceholder: '非 Mock 模式下必填',
        analysisLang: 'AI 解析语言',
        maxBatch: '最大批处理大小',
        adaptiveBatch: '自适应最大阈值',
        fixedBatch: '固定批处理大小',
        adaptiveDesc: '算法根据背压状态动态调整批次大小。',
        fixedDesc: '系统强制等待缓冲区填满才触发处理。',
        promptTemplate: '提示词模板',
        preview: '预览',
        promptList: '提示词列表',
        newPrompt: '新建提示词',
        promptName: '提示词名称',
        templateContent: '模板内容'
      },
      integration: {
        threshold: '报警阈值',
        critical: '严重 (Critical)',
        error: '错误 (Error)',
        warning: '警告 (Warning)',
        hint: 'High: 仅关键安全风险。 Low: 所有警告及以上。',
        channels: '通知渠道',
        newChannel: '新建渠道',
        vendor: '厂商类型',
        webhookName: 'Webhook 名称',
        webhookUrl: 'Webhook 地址',
        payloadTemplate: '消息模板 (JSON)',
        testMessage: '发送测试消息',
        testSuccess: '测试消息已发送！'
      },
      kernel: {
        worker: '工作线程数',
        ioBuffer: 'IO 缓冲区大小',
        wal: 'SQLite WAL 同步',
        walTitle: '预写式日志 (WAL)',
        walDesc: '启用同步检查点以保证数据持久性。',
        adaptiveMode: '自适应微批模式',
        adaptiveLabel: '自适应模式',
        fixedLabel: '固定模式',
        flushInterval: '刷新间隔',
        flushDesc: '无论缓冲是否填满，每 N 毫秒强制刷新。'
      },
      save: '保存配置',
      success: '配置已应用'
    }
  }
}

const i18n = createI18n({
  legacy: false, // Use Composition API
  locale: 'en',
  fallbackLocale: 'en',
  globalInjection: true, // Enables $t in templates
  messages
})

export default i18n
