<template>
  <div class="h-full flex flex-col p-6 space-y-6">

    <!-- 顶部: 核心指标卡片 -->
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <TokenMetricsCard
            :title="$t('aiEngine.metrics.totalTokens')"
            :value="metrics.totalTokens.toLocaleString()"
            icon="Coin"
            trend="up"
            subtext="+12% vs yesterday"
        />
        <TokenMetricsCard
            :title="$t('aiEngine.metrics.savings')"
            :value="metrics.savingsPercentage + '%'"
            icon="Money"
            status="success"
            trend="up"
            subtext="Optimization active"
        />
        <TokenMetricsCard
            :title="$t('aiEngine.metrics.estCost')"
            value="$0.45"
            icon="Wallet"
            status="info"
        />
        <TokenMetricsCard
            :title="$t('aiEngine.metrics.avgTokens')"
            :value="metrics.avgTokensPerBatch"
            icon="Files"
            subtext="Per batch"
        />
    </div>

    <!-- 底部: 批次存档列表 -->
    <div class="flex-1 min-h-0">
        <BatchArchiveList :data="archiveData" @debug="openDebugger" />
    </div>

    <!-- 调试器抽屉 -->
    <PromptDebugger v-model="showDebugger" :data="currentDebugData" />

  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import TokenMetricsCard from '../components/TokenMetricsCard.vue'
import BatchArchiveList from '../components/BatchArchiveList.vue'
import PromptDebugger from '../components/PromptDebugger.vue'
import type { TokenMetrics, BatchArchiveItem, PromptDebugInfo } from '../types/aiEngine'
import dayjs from 'dayjs'

// --- 状态管理 ---
const showDebugger = ref(false)
const currentDebugData = ref<PromptDebugInfo | null>(null)

const metrics = ref<TokenMetrics>({
    totalTokens: 125840,
    savingsPercentage: 85,
    estimatedCost: 0.45,
    avgTokensPerBatch: 512
})

const archiveData = ref<BatchArchiveItem[]>([])

// --- Mock 数据生成 ---
const MOCK_SUMMARIES = [
    "Traffic patterns nominal. User authentication flows behaving as expected.",
    "Detected slight elevation in 404 responses from /api/v1/user endpoints.",
    "SECURITY ALERT: Coordinated SQL Injection attempt detected across 3 distinct IPs.",
    "High memory usage detected in Worker #3. Garbage collection frequency increased.",
    "Database query latency average is stable at 45ms. Ingress throughput steady."
]

const MOCK_TAGS = [
    ['auth', 'nominal'],
    ['404', 'crawler'],
    ['security', 'sqli', 'attack'],
    ['system', 'memory'],
    ['db', 'performance']
]

const MOCK_RISKS = ['SAFE', 'WARNING', 'CRITICAL', 'WARNING', 'SAFE']

const generateMockData = () => {
    const data: BatchArchiveItem[] = []
    const now = dayjs()

    for(let i=0; i<20; i++) {
        const idx = i % 5
        data.push({
            id: (1024 - i).toString(),
            createdAt: now.subtract(i * 5, 'minute').format('HH:mm:ss'),
            logCount: Math.floor(Math.random() * 50) + 10,
            riskLevel: MOCK_RISKS[idx] as any,
            tokenCount: Math.floor(Math.random() * 1000) + 200,
            avgLatency: Math.floor(Math.random() * 100) + 20,
            summary: MOCK_SUMMARIES[idx],
            tags: MOCK_TAGS[idx]
        })
    }
    archiveData.value = data
}

const openDebugger = (item: BatchArchiveItem) => {
    // 根据 item 生成 Mock 调试信息
    currentDebugData.value = {
        batchId: item.id,
        input: {
            trace_context: {
                logs: Array(5).fill({ ts: "2024-01-01T10:00:00Z", msg: "Mock log entry..." }),
                span_count: item.logCount
            },
            constraint: "Analyze for security risks and performance bottlenecks.",
            system_prompt: "You are LogSentinel AI. Analyze the following logs..."
        },
        output: {
            risk_level: item.riskLevel,
            summary: item.summary,
            root_cause: item.riskLevel === 'CRITICAL' ? "SQL Injection payload detected in query params." : "None",
            solution: item.riskLevel === 'CRITICAL' ? "Block IP range and sanitize inputs." : "Monitor for changes."
        },
        meta: {
            timestamp: dayjs().format(),
            model: "gemini-pro",
            latency: item.avgLatency + 200, // 推理时间通常比批处理时间长
            tokens: item.tokenCount
        }
    }
    showDebugger.value = true
}

onMounted(() => {
    generateMockData()
})
</script>
