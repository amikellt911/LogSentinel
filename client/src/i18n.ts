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
      configuration: 'System Configuration',
      insights: 'AI Batch Insights',
      history: 'Historical Logs',
      benchmark: 'Performance Arena'
    },
    benchmark: {
      tool: 'Benchmark Tool',
      threads: 'Threads',
      connections: 'Connections',
      duration: 'Duration',
      start: 'Start Benchmark',
      running: 'RUNNING BENCHMARK...',
      metrics: {
        qps: 'QPS (Throughput)',
        p50: 'P50 Latency',
        p99: 'P99 Latency',
        net_io: 'Net I/O Latency',
        scheduler: 'AI Scheduler Cost',
        total_logs: 'Total Logs Processed',
        total_ai: 'Total AI Calls'
      }
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
      aiRate: 'AI Throughput',
      realtimeTitle: 'Real-time Ingestion vs AI Processing',
      calls: 'calls',
      netLatency: 'Task Queue Time',
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
    history: {
      riskLevel: 'Risk Level',
      all: 'All',
      search: 'Search',
      placeholder: 'Search logs by keyword or Trace ID...',
      refresh: 'Refresh',
      details: 'Details',
      table: {
        time: 'Time',
        level: 'Level',
        summary: 'Summary',
        traceId: 'Trace ID',
        actions: 'Actions'
      },
      dialog: {
        title: 'Log Details',
        aiSummary: 'AI Analysis Summary',
        rawContent: 'Raw Log Content (Mock)'
      }
    },
    insights: {
      title: 'AI Insight Stream (Reduce Phase)',
      subtitle: 'Real-time aggregation of log batches. Map-Reduce pipeline active.',
      lastBatch: 'Last Batch',
      context: 'Global Context Window',
      batch: 'BATCH',
      size: 'Size',
      logs: 'logs',
      risks: 'Risks',
      latency: 'Latency',
      aiSummary: 'AI Summary',
      waiting: 'Waiting for next batch processing window...'
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
        language: 'Application Language',
        logRetention: 'Log Retention Strategy',
        retentionPeriod: 'Retention Period',
        days: 'days',
        maxDisk: 'Max Disk Usage',
        network: 'Network Configuration',
        httpPort: 'HTTP Port'
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
        resilienceTitle: 'Resilience & Reliability',
        autoDegrade: 'Auto-Degradation',
        enableDegrade: 'Enable Degradation',
        fallbackModel: 'Fallback Model Name',
        circuitBreaker: 'Circuit Breaker',
        enableBreaker: 'Enable Circuit Breaker',
        failureThreshold: 'Failure Threshold',
        cooldown: 'Cool-down Period',
        seconds: 'seconds',
        promptTemplate: 'Prompt Template',
          preview: 'PREVIEW',
          promptList: 'Prompts',
          newPrompt: 'New Prompt',
          promptName: 'Prompt Name',
          templateContent: 'Template Content',
          mapPhase: 'Map Phase',
          reducePhase: 'Reduce Phase'
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
      configuration: '系统配置',
      insights: 'AI 批次洞察',
      history: '历史日志',
      benchmark: '性能竞技场'
    },
    benchmark: {
      tool: '压测工具',
      threads: '线程数',
      connections: '并发连接数',
      duration: '持续时间',
      start: '开始基准测试',
      running: '基准测试运行中...',
      metrics: {
        qps: 'QPS (吞吐量)',
        p50: 'P50 延迟',
        p99: 'P99 延迟',
        net_io: '网络 I/O 延迟',
        scheduler: 'AI 调度开销',
        total_logs: '日志处理总数',
        total_ai: 'AI 调用总数'
      }
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
      aiRate: 'AI 吞吐量',
      realtimeTitle: '实时摄入 vs AI 处理',
      calls: '次调用',
      netLatency: '任务排队时间',
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
    history: {
      riskLevel: '风险等级',
      all: '全部',
      search: '搜索',
      placeholder: '按关键词或 Trace ID 搜索...',
      refresh: '刷新',
      details: '详情',
      table: {
        time: '时间',
        level: '级别',
        summary: '摘要',
        traceId: '追踪 ID',
        actions: '操作'
      },
      dialog: {
        title: '日志详情',
        aiSummary: 'AI 分析摘要',
        rawContent: '原始日志内容 (Mock)'
      }
    },
    insights: {
      title: 'AI 态势感知流 (Reduce 阶段)',
      subtitle: '日志批次实时聚合。Map-Reduce 管道运行中。',
      lastBatch: '最新批次',
      context: '全局上下文窗口',
      batch: '批次',
      size: '大小',
      logs: '条',
      risks: '风险数',
      latency: '延迟',
      aiSummary: 'AI 总结',
      waiting: '正在等待下一个批次处理窗口...'
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
        language: '系统语言',
        logRetention: '日志存储策略',
        retentionPeriod: '保留天数',
        days: '天',
        maxDisk: '最大磁盘占用',
        network: '网络监听配置',
        httpPort: 'HTTP 端口'
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
        resilienceTitle: '高可用与容灾',
        autoDegrade: '自动降级',
        enableDegrade: '启用降级',
        fallbackModel: '降级模型名称',
        circuitBreaker: '熔断机制',
        enableBreaker: '启用熔断',
        failureThreshold: '触发阈值',
        cooldown: '冷却时间',
        seconds: '秒',
        promptTemplate: '提示词模板',
        preview: '预览',
        promptList: '提示词列表',
        newPrompt: '新建提示词',
        promptName: '提示词名称',
        templateContent: '模板内容',
        mapPhase: 'Map 阶段',
        reducePhase: 'Reduce 阶段'
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
