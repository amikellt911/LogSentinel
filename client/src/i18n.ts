import { createI18n } from 'vue-i18n'

const messages = {
  en: {
    common: {
      copy: 'Copy',
      copySuccess: 'Copied to clipboard',
      copyFailed: 'Failed to copy',
      operations: 'Actions'
    },
    layout: {
      serviceMonitor: 'Service Monitor',
      dashboard: 'System Status',
      logs: 'Live Logs',
      settings: 'Settings',
      systemRunning: 'SYSTEM RUNNING',
      systemIdle: 'SYSTEM IDLE',
      missionControl: 'Mission Control Dashboard',
      eventStream: 'Real-time Event Stream',
      configuration: 'System Configuration',
      insights: 'AI Batch Insights',
      traceExplorer: 'Trace Explorer',
      aiEngine: 'AI Engine',
      history: 'Historical Logs',
      benchmark: 'Performance Arena',
      simMode: 'SIM MODE'
    },
    aiEngine: {
      title: 'AI Engine Center',
      subtitle: 'Token consumption monitoring and batch archive management',
      status: 'Status',
      running: 'Running',
      model: 'Model',
      todayTotalTokens: 'Today\'s Total Tokens',
      tokens: 'Tokens',
      savingRatio: 'Saving Ratio',
      comparedToRaw: 'vs. raw logs',
      estimatedCost: 'Estimated Cost',
      today: 'today',
      avgTokensPerBatch: 'Avg Tokens/Batch',
      perBatch: 'per batch',
      batchArchive: 'Batch Archive',
      last1h: 'Last 1h',
      last6h: 'Last 6h',
      last24h: 'Last 24h',
      last7d: 'Last 7d',
      customTime: 'Custom',
      allRisks: 'All Risks',
      criticalOnly: 'Critical Only',
      errorOnly: 'Error Only',
      warningOnly: 'Warning Only',
      traceId: 'Trace ID',
      traceIdPlaceholder: 'Enter Trace ID',
      trace: 'Trace',
      createdAt: 'Created At',
      logCount: 'Log Count',
      logs: 'logs',
      spanCount: 'Span Count',
      riskLevel: 'Risk Level',
      timeRange: 'Time Range',
      tokenCount: 'Tokens',
      viewDetails: 'View Details',
      collapse: 'Collapse',
      promptDebugger: 'Prompt Debugger',
      total: 'Total',
      batches: 'batches',
      input: 'Input',
      output: 'Output',
      duration: 'Duration',
      totalTokens: 'Total Tokens',
      timestamp: 'Timestamp'
    },
    serviceMonitor: {
      subtitle: 'Real-time business health monitoring and AI situation awareness',
      healthScore: {
        title: 'Today\'s Health Score',
        label: 'vs. yesterday'
      },
      errorRate: {
        title: 'Current Error Rate',
        label: 'of total requests'
      },
      avgDuration: {
        title: 'Average Duration',
        label: 'per request'
      },
      aiStream: {
        title: 'AI Situation Awareness Stream',
        subtitle: 'Real-time log batch aggregation. Map-Reduce pipeline active.',
        pause: 'Pause',
        resume: 'Resume',
        loadMore: 'Load More'
      }
    },
    traceExplorer: {
      search: {
        traceId: 'Trace ID',
        traceIdPlaceholder: 'Enter Trace ID',
        serviceName: 'Service',
        allServices: 'All Services',
        timeRange: 'Time Range',
        startTime: 'Start Time',
        endTime: 'End Time',
        riskLevel: 'Risk Level',
        allLevels: 'All Levels',
        minDuration: 'Min Duration (ms)',
        minDurationPlaceholder: 'Min Duration',
        tags: 'Tags',
        tagPlaceholder: 'Search by tags...',
        reset: 'Reset',
        search: 'Search'
      },
      table: {
        traceId: 'Trace ID',
        serviceName: 'Service Name',
        startTime: 'Start Time',
        duration: 'Duration',
        spanCount: 'Span Count',
        tokenCount: 'Tokens',
        riskLevel: 'Risk Level',
        actions: 'Actions',
        aiAnalysis: 'AI Analysis',
        callChain: 'Call Chain',
        promptDebugger: 'Prompt',
        viewDetails: 'Details'
      },
      waterfall: {
        title: 'Trace Waterfall Visualization',
        spanId: 'Span ID',
        operation: 'Operation',
        startTime: 'Start Time',
        duration: 'Duration',
        status: 'Status'
      },
      drawer: {
        title: 'Trace Details',
        aiAnalysis: 'AI Root Cause Analysis',
        summary: 'Summary',
        rootCause: 'Root Cause',
        solution: 'Solution',
        noAnalysis: 'No AI analysis available',
        spanList: 'Span List'
      },
      pagination: {
        total: 'Total'
      }
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
    common: {
      copy: '复制',
      copySuccess: '已复制到剪贴板',
      copyFailed: '复制失败',
      operations: '操作'
    },
    layout: {
      serviceMonitor: '服务监控',
      dashboard: '系统状态',
      logs: '实时日志',
      settings: '系统设置',
      systemRunning: '系统运行中',
      systemIdle: '系统待机',
      missionControl: '任务控制仪表盘',
      eventStream: '实时事件流',
      configuration: '系统配置',
      insights: '智能分析',
      traceExplorer: 'Trace 追溯',
      aiEngine: 'AI 引擎中心',
      history: '历史日志',
      benchmark: '基准测试',
      simMode: '模拟模式'
    },
    aiEngine: {
      title: 'AI 引擎中心',
      subtitle: 'Token 消耗监控与批次存档管理',
      status: '状态',
      running: '运行中',
      model: '模型',
      todayTotalTokens: '今日 Token 总量',
      tokens: 'Tokens',
      savingRatio: '节省比例',
      comparedToRaw: '对比原始日志',
      estimatedCost: '预估成本',
      today: '今日',
      avgTokensPerBatch: '平均 Token/批次',
      perBatch: '每批次',
      batchArchive: '批次存档',
      last1h: '最近 1 小时',
      last6h: '最近 6 小时',
      last24h: '最近 24 小时',
      last7d: '最近 7 天',
      customTime: '自定义',
      allRisks: '全部风险',
      criticalOnly: '仅严重',
      errorOnly: '仅错误',
      warningOnly: '仅警告',
      traceId: 'Trace ID',
      traceIdPlaceholder: '输入 Trace ID',
      trace: 'Trace',
      createdAt: '创建时间',
      logCount: '日志数量',
      logs: '条',
      spanCount: 'Span 数量',
      riskLevel: '风险等级',
      timeRange: '时间范围',
      tokenCount: 'Token 数',
      viewDetails: '查看详情',
      collapse: '折叠',
      promptDebugger: 'Prompt 透视',
      total: '总计',
      batches: '批次',
      input: '输入',
      output: '输出',
      duration: '耗时',
      totalTokens: '总 Token',
      timestamp: '时间戳'
    },
    serviceMonitor: {
      subtitle: '实时业务健康监控与 AI 态势感知',
      healthScore: {
        title: '今日业务健康分',
        label: '较昨日'
      },
      errorRate: {
        title: '当前错误率',
        label: '占请求总数'
      },
      avgDuration: {
        title: '平均业务耗时',
        label: '每次请求'
      },
      aiStream: {
        title: 'AI 态势感知流',
        subtitle: '日志批次实时聚合。Map-Reduce 管道运行中。',
        pause: '暂停',
        resume: '继续',
        loadMore: '加载更多'
      }
    },
    traceExplorer: {
      search: {
        traceId: 'Trace ID',
        traceIdPlaceholder: '输入 Trace ID',
        serviceName: '服务名称',
        allServices: '全部服务',
        timeRange: '时间范围',
        startTime: '开始时间',
        endTime: '结束时间',
        riskLevel: '风险等级',
        allLevels: '全部等级',
        minDuration: '最小耗时（ms）',
        minDurationPlaceholder: '最小耗时',
        tags: '标签',
        tagPlaceholder: '按标签搜索...',
        reset: '重置',
        search: '搜索'
      },
      table: {
        traceId: 'Trace ID',
        serviceName: '服务名称',
        startTime: '开始时间',
        duration: '耗时',
        spanCount: 'Span 数量',
        tokenCount: 'Token 数',
        riskLevel: '风险等级',
        actions: '操作',
        aiAnalysis: 'AI 分析',
        callChain: '调用链',
        promptDebugger: 'Prompt',
        viewDetails: '详情'
      },
      waterfall: {
        title: 'Trace 瀑布图可视化',
        spanId: 'Span ID',
        operation: '操作',
        startTime: '开始时间',
        duration: '耗时',
        status: '状态'
      },
      drawer: {
        title: 'Trace 详情',
        aiAnalysis: 'AI 根因分析',
        summary: '总结',
        rootCause: '根因',
        solution: '解决方案',
        noAnalysis: '暂无 AI 分析',
        spanList: 'Span 列表'
      },
      pagination: {
        total: '总计'
      }
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
