import { createI18n } from 'vue-i18n'

const messages = {
  en: {
    common: {
      copy: 'Copy',
      copySuccess: 'Copied to clipboard',
      copyFailed: 'Failed to copy',
      operations: 'Actions',
      delete: 'Delete',
      cancel: 'Cancel'
    },
    layout: {
      serviceMonitor: 'Service Monitor',
      servicePrototype: 'Service Prototype',
      dashboard: 'System Monitor',
      logs: 'Live Logs',
      settings: 'Settings',
      missionControl: 'Mission Control Dashboard',
      eventStream: 'Real-time Event Stream',
      configuration: 'System Configuration',
      insights: 'AI Batch Insights',
      traceExplorer: 'Trace Explorer',
      aiEngine: 'AI Engine',
      history: 'Historical Logs',
      benchmark: 'Performance Arena'
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
      avgTokensPerCall: 'Avg Tokens/Call',
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
        subtitle: 'Real-time log batch aggregation with trace-aware grouping.',
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
        serviceNameExactPlaceholder: 'Enter entry service name (exact match)',
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
        aiStatus: 'AI Status',
        actions: 'Actions',
        aiAnalysis: 'AI Analysis',
        callChain: 'Call Chain',
        promptDebugger: 'Prompt',
        viewDetails: 'Details',
        deleteTrace: 'Delete'
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
      // Trace 读侧统一复用 ai_status 文案，避免表格和详情各写一套字符串。
      aiStatus: {
        completed: 'Completed',
        skippedManual: 'Disabled',
        skippedCircuit: 'Skipped by Circuit Breaker',
        failedPrimary: 'Primary Failed',
        failedBoth: 'Both Failed',
        pending: 'Pending',
        skippedManualDesc: 'AI analysis was manually disabled for this trace. Only aggregated data is available.',
        skippedCircuitDesc: 'AI analysis was skipped because the circuit breaker is active.',
        failedPrimaryDesc: 'The primary AI analysis failed, so no analysis result was generated for this trace.',
        failedBothDesc: 'Both the primary and fallback AI analysis failed, so no analysis result was generated.',
        pendingDesc: 'AI analysis is still in progress.'
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
      totalLogs: 'Total Logs',
      logs: 'logs',
      start: 'Start Benchmark',
      running: 'RUNNING BENCHMARK...',
      tabs: {
        qps: 'QPS Test',
        cost: 'Cost Analysis'
      },
      metrics: {
        qps: 'QPS (Throughput)',
        p50: 'P50 Latency',
        p99: 'P99 Latency',
        total_logs: 'Total Logs Processed',
        ai_calls: 'AI Calls',
        input_tokens: 'Input Tokens',
        output_tokens: 'Output Tokens',
        estimated_cost: 'Estimated Cost'
      },
      priceRef: {
        title: 'Price Reference (DeepSeek-V3)',
        model: 'Model',
        inputPrice: 'Input Tokens',
        outputPrice: 'Output Tokens'
      }
    },
    dashboard: {
      totalLogs: 'Total Logs Processed',
      aiTrigger: 'AI Call Count',
      backpressure: 'Back-pressure Status',
      memory: 'Memory Usage',
      normal: 'Normal',
      active: 'Active',
      full: 'Full',
      stable: 'System Stable',
      throttling: 'Throttling Active',
      congested: 'Queue Congested',
      ingestRate: 'Ingest Rate',
      aiRate: 'AI Completion Rate',
      realtimeTitle: 'System Throughput Trend',
      calls: 'calls',
      netLatency: 'AI Queue Wait Time',
      aiLatency: 'AI Inference Latency',
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
      title: 'AI Insight Stream (Trace Summary)',
      subtitle: 'Trace-based aggregation before a single LLM call.',
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
    },
    messages: {
      traceExplorer: {
        customRangeMissing: 'Custom time range requires both start time and end time.',
        customRangeInvalid: 'Invalid custom time format.',
        customRangeOrder: 'End time must be later than start time.',
        listFetchFailed: 'Failed to query trace list',
        detailFetchFailed: 'Failed to query trace details',
        deleteFailed: 'Failed to delete trace',
        deleteSuccess: 'Trace deleted successfully',
        deleteConfirmTitle: 'Delete Confirmation',
        deleteConfirmBody: 'Delete trace {traceId}? This will also remove the summary, spans, and AI analysis.'
      },
      settingsPrototype: {
        loadFailed: 'Failed to load settings. Local defaults are kept for now.',
        resetSuccess: 'Reverted to the last saved settings.',
        webhookMissing: 'Please fill in the Webhook URL before sending a test message.',
        probeWithSecret: 'Signature secret attached',
        probeWithoutSecret: 'No signature secret provided',
        probeSuccess: 'Simulated test message sent: {name} ({secretState})',
        saveSuccess: 'Settings saved successfully',
        saveSuccessRestart: 'Settings saved successfully: includes fields that take effect after restart',
        saveFailed: 'Failed to save settings',
        defaultPromptName: 'Default Prompt',
        defaultChannelName: 'Default Feishu Channel',
        newPromptName: 'New Business Prompt {id}',
        newChannelName: 'New Feishu Channel {id}'
      }
    }
  },
  zh: {
    common: {
      copy: '复制',
      copySuccess: '已复制到剪贴板',
      copyFailed: '复制失败',
      operations: '操作',
      delete: '删除',
      cancel: '取消'
    },
    layout: {
      serviceMonitor: '服务监控',
      servicePrototype: '服务监控原型',
      dashboard: '系统监控',
      logs: '实时日志',
      settings: '系统设置',
      missionControl: '任务控制仪表盘',
      eventStream: '实时事件流',
      configuration: '系统配置',
      insights: '智能分析',
      traceExplorer: 'Trace 追溯',
      aiEngine: 'AI 引擎中心',
      history: '历史日志',
      benchmark: '基准测试'
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
      avgTokensPerCall: '平均 Token/次调用',
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
        subtitle: '日志批次实时聚合，按 trace 汇总后统一分析。',
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
        serviceNameExactPlaceholder: '输入入口服务名（精确匹配）',
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
        aiStatus: 'AI 状态',
        actions: '操作',
        aiAnalysis: 'AI 分析',
        callChain: '调用链',
        promptDebugger: 'Prompt',
        viewDetails: '详情',
        deleteTrace: '删除'
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
      // Trace 读侧统一复用 ai_status 文案，避免表格和详情各写一套字符串。
      aiStatus: {
        completed: '已完成',
        skippedManual: '已关闭',
        skippedCircuit: '熔断跳过',
        failedPrimary: '主路失败',
        failedBoth: '双路失败',
        pending: '处理中',
        skippedManualDesc: 'AI 分析已被手动关闭，本次 trace 只保留聚合结果。',
        skippedCircuitDesc: 'AI 当前处于熔断跳过状态，本次 trace 未发起分析。',
        failedPrimaryDesc: '主 AI 分析失败，本次 trace 没有生成分析结果。',
        failedBothDesc: '主 AI 和降级 AI 都失败了，本次 trace 没有生成分析结果。',
        pendingDesc: 'AI 分析尚未完成。'
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
      totalLogs: '日志总数',
      logs: '条',
      start: '开始基准测试',
      running: '基准测试运行中...',
      tabs: {
        qps: 'QPS 测试',
        cost: '开销分析'
      },
      metrics: {
        qps: 'QPS (吞吐量)',
        p50: 'P50 延迟',
        p99: 'P99 延迟',
        total_logs: '日志处理总数',
        ai_calls: 'AI 调用次数',
        input_tokens: '输入 Tokens',
        output_tokens: '输出 Tokens',
        estimated_cost: '预计花费'
      },
      priceRef: {
        title: '价格参考 (DeepSeek-V3)',
        model: '模型',
        inputPrice: '输入 Tokens',
        outputPrice: '输出 Tokens'
      }
    },
    dashboard: {
      totalLogs: '日志处理总数',
      aiTrigger: 'AI 调用总数',
      backpressure: '背压状态',
      memory: '内存占用',
      normal: '正常',
      active: '激活',
      full: '满载',
      stable: '系统稳定',
      throttling: '限流中',
      congested: '队列拥堵',
      ingestRate: '接入速率',
      aiRate: 'AI 完成速率',
      realtimeTitle: '系统吞吐趋势',
      calls: '次调用',
      netLatency: 'AI 队列等待时间',
      aiLatency: 'AI 推理延迟',
      queue: '队列占用',
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
      title: 'AI 态势感知流（Trace 汇总）',
      subtitle: '按 trace 聚合日志批次后再统一调用模型。',
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
    },
    messages: {
      traceExplorer: {
        customRangeMissing: '自定义时间范围需要同时填写开始时间和结束时间',
        customRangeInvalid: '自定义时间格式无效',
        customRangeOrder: '结束时间必须晚于开始时间',
        listFetchFailed: 'Trace 列表查询失败',
        detailFetchFailed: 'Trace 详情查询失败',
        deleteFailed: 'Trace 删除失败',
        deleteSuccess: 'Trace 删除成功',
        deleteConfirmTitle: '删除确认',
        deleteConfirmBody: '确认删除 Trace {traceId} 吗？此操作会同时删除 summary、span 和 AI analysis。'
      },
      settingsPrototype: {
        loadFailed: '设置加载失败，当前先保留本地默认值',
        resetSuccess: '已恢复到上一次保存的设置',
        webhookMissing: '请先填写 Webhook URL，再发送测试消息',
        probeWithSecret: '已附带签名 Secret',
        probeWithoutSecret: '未填写签名 Secret',
        probeSuccess: '已模拟发送测试消息：{name}（{secretState}）',
        saveSuccess: '设置已保存',
        saveSuccessRestart: '设置已保存：包含重启后生效字段',
        saveFailed: '设置保存失败',
        defaultPromptName: '默认 Prompt',
        defaultChannelName: '默认飞书渠道',
        newPromptName: '新业务 Prompt {id}',
        newChannelName: '新飞书渠道 {id}'
      }
    }
  }
}

const i18n = createI18n({
  legacy: false, // Use Composition API
  // 默认先用中文，避免入口预取设置失败时整站先回退成英文。
  // 真正的最终语言仍然由 main.ts 预取到的 app_language 再覆盖。
  locale: 'zh',
  fallbackLocale: 'zh',
  globalInjection: true, // Enables $t in templates
  messages
})

export default i18n
