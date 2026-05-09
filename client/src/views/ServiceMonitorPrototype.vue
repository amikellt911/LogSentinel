<template>
  <div class="h-full overflow-y-auto custom-scrollbar bg-[#0b0f17] text-gray-200">
    <div class="p-6 space-y-6">
      <!-- 顶部标题区：这里只负责告诉用户这是一张“服务视角首页”，不是 Trace 检索页 -->
      <div class="flex flex-col gap-4 xl:flex-row xl:items-start xl:justify-between">
        <div>
          <h1 class="text-3xl font-bold text-white tracking-wide">{{ $t('serviceMonitor.prototype.title') }}</h1>
          <p class="text-sm text-gray-400 mt-2">
            {{ $t('serviceMonitor.prototype.subtitle') }}
          </p>
        </div>

        <div class="flex flex-wrap items-center gap-3">
          <div class="rounded-lg border border-gray-700 bg-gray-900/70 px-3 py-2 text-sm text-gray-300">
            {{ $t('serviceMonitor.prototype.rangeRecent30m') }}
          </div>
          <button
            class="rounded-lg border border-gray-700 bg-gray-900/70 px-4 py-2 text-sm text-gray-200 hover:bg-gray-800"
            type="button"
            :disabled="manualRefreshLoading"
            @click="refreshRuntimeSnapshotManually"
          >
            {{ manualRefreshLoading ? $t('serviceMonitor.prototype.refreshing') : $t('serviceMonitor.prototype.refresh') }}
          </button>
          <button
            class="rounded-lg bg-blue-500 px-4 py-2 text-sm font-semibold text-white hover:bg-blue-400"
            type="button"
            @click="goTraceExplorer"
          >
            {{ $t('serviceMonitor.prototype.openTraces') }}
          </button>
        </div>
      </div>

      <!-- 总览卡片：只保留用户一眼能看懂的全局信息 -->
      <div class="grid grid-cols-1 gap-4 md:grid-cols-2 xl:grid-cols-3">
        <div
          v-for="card in overviewCards"
          :key="card.label"
          class="rounded-2xl border border-gray-800 bg-[linear-gradient(135deg,rgba(25,32,45,0.95),rgba(17,21,30,0.95))] p-5 shadow-[0_0_0_1px_rgba(255,255,255,0.02)]"
        >
          <div class="text-sm uppercase tracking-[0.2em] text-gray-500">{{ card.label }}</div>
          <div class="mt-4 text-4xl font-bold" :class="card.valueClass">{{ card.value }}</div>
          <div class="mt-3 text-sm text-gray-400">{{ card.desc }}</div>
        </div>
      </div>

      <div class="grid grid-cols-1 gap-6 xl:grid-cols-[3fr_2fr]">
        <div class="space-y-6">
          <!-- 左侧服务卡：用户先扫一眼“最近哪些服务值得看” -->
          <section class="space-y-4">
          <div class="flex items-center justify-between">
              <div>
                <h2 class="text-xl font-semibold text-white">{{ $t('serviceMonitor.prototype.overviewTitle') }}</h2>
                <div class="mt-1 text-sm text-gray-500">{{ $t('serviceMonitor.prototype.serviceOverviewHint') }}</div>
              </div>
              <span class="text-xs uppercase tracking-[0.25em] text-gray-500">{{ $t('serviceMonitor.prototype.serviceFirst') }}</span>
            </div>

            <div class="grid grid-cols-1 gap-4 lg:grid-cols-2">
              <div
                v-if="services.length === 0"
                class="rounded-2xl border border-dashed border-gray-700 bg-black/20 p-6 text-sm leading-7 text-gray-400 lg:col-span-2"
              >
                {{ $t('serviceMonitor.prototype.emptyServices') }}{{ $t('serviceMonitor.prototype.emptyServicesHint') }}
              </div>
              <button
                v-for="service in services"
                :key="service.name"
                type="button"
                class="group rounded-2xl border bg-[linear-gradient(180deg,rgba(23,27,38,0.95),rgba(15,18,27,0.95))] p-5 text-left transition-all hover:-translate-y-0.5"
                :class="serviceCardClass(service)"
                @click="selectedServiceName = service.name"
              >
                <div class="flex items-start justify-between gap-4">
                  <div>
                    <div class="flex items-center gap-3">
                      <span class="h-3 w-3 rounded-full" :class="riskDotClass(service.risk)"></span>
                      <span class="text-2xl font-semibold text-white">{{ service.name }}</span>
                    </div>
                    <div class="mt-4 text-sm text-gray-400">
                      {{ $t('serviceMonitor.prototype.recentExceptionCount') }} <span class="font-mono text-gray-200">{{ service.exceptionCount }}</span> {{ $t('serviceMonitor.prototype.callsSuffix') }}
                    </div>
                    <div class="mt-1 text-xs text-gray-500">{{ $t('serviceMonitor.prototype.dedupHint') }}</div>
                  </div>

                  <span
                    class="rounded-full border px-3 py-1 text-xs font-semibold uppercase tracking-wider"
                    :class="riskBadgeClass(service.risk)"
                  >
                    {{ statusText(service.risk) }}
                  </span>
                </div>

              <div class="mt-5 rounded-xl bg-black/20 p-3 text-sm">
                  <div class="text-gray-500">{{ $t('serviceMonitor.prototype.latestExceptionTime') }}</div>
                  <div class="mt-1 text-xl font-mono text-white">{{ service.latestExceptionTime }}</div>
              </div>

                <div class="mt-4">
                  <div class="text-xs uppercase tracking-[0.2em] text-gray-500">{{ $t('serviceMonitor.prototype.latestExceptionSummary') }}</div>
                  <div class="mt-2 min-h-[44px] text-sm leading-6 text-gray-300">
                    {{ service.summary }}
                  </div>
                </div>

                <div class="mt-4 flex items-center justify-end">
                  <span class="text-xs text-gray-500">{{ $t('serviceMonitor.prototype.openFocusPanel') }}</span>
                </div>
              </button>
            </div>
          </section>

          <!-- 当前服务最近异常 Trace：和左侧服务卡同列堆叠，避免被右侧大面板撑出空白 -->
          <section class="rounded-3xl border border-gray-800 bg-[linear-gradient(180deg,rgba(20,24,34,0.95),rgba(14,17,24,0.95))] p-6">
            <div class="flex items-center justify-between">
              <div>
                <h2 class="text-xl font-semibold text-white">{{ $t('serviceMonitor.prototype.recentTraceSamples') }}</h2>
                <div class="mt-1 text-sm text-gray-500">
                  {{ $t('serviceMonitor.prototype.recentTraceSamplesHint', { service: selectedService.name }) }}
                </div>
              </div>
              <button
                class="rounded-xl border border-gray-700 bg-gray-900/70 px-4 py-2 text-sm text-gray-100 hover:bg-gray-800"
                type="button"
                @click="goTraceExplorer"
              >
                {{ $t('serviceMonitor.prototype.moreRelatedTraces') }}
              </button>
            </div>

            <div class="mt-5 space-y-4">
              <div
                v-if="selectedService.recentTraces.length === 0"
                class="rounded-2xl border border-dashed border-gray-700 bg-black/20 p-4 text-sm leading-7 text-gray-400"
              >
                {{ $t('serviceMonitor.prototype.emptyRecentTraces') }}
              </div>
              <div
                v-for="trace in selectedService.recentTraces"
                :key="trace.traceId"
                class="rounded-2xl border border-gray-800 bg-black/20 p-4"
              >
                <div class="flex flex-col gap-4 xl:flex-row xl:items-start xl:justify-between">
                  <div class="min-w-0 flex-1">
                    <div class="flex flex-wrap items-center gap-2">
                      <span class="text-sm font-mono text-gray-400">{{ trace.time }}</span>
                      <span
                        class="rounded-full border px-2 py-1 text-xs font-semibold uppercase tracking-wider"
                        :class="riskBadgeClass(trace.risk)"
                      >
                        {{ statusText(trace.risk) }}
                      </span>
                      <span class="rounded-full bg-gray-900/70 px-2 py-1 text-xs font-mono text-gray-300">
                        {{ trace.operation }}
                      </span>
                    </div>

                    <div class="mt-3 text-base leading-7 text-gray-200">
                      {{ trace.summary }}
                    </div>

                    <div class="mt-4 grid grid-cols-1 gap-3 md:grid-cols-2">
                      <div class="rounded-xl bg-gray-900/60 px-3 py-3">
                        <div class="text-xs uppercase tracking-wider text-gray-500">{{ $t('serviceMonitor.prototype.traceId') }}</div>
                        <div class="mt-1 break-all font-mono text-sm text-gray-200">{{ trace.traceId }}</div>
                      </div>
                      <div class="rounded-xl bg-gray-900/60 px-3 py-3">
                        <div class="text-xs uppercase tracking-wider text-gray-500">{{ $t('serviceMonitor.prototype.traceDuration') }}</div>
                        <div class="mt-1 font-mono text-sm text-gray-200">{{ trace.durationMs }}ms</div>
                      </div>
                    </div>
                  </div>

                  <div class="flex shrink-0 items-center gap-3">
                    <button
                      class="rounded-xl border border-gray-700 bg-gray-900/70 px-4 py-2 text-sm text-gray-100 hover:bg-gray-800"
                      type="button"
                      @click="goTraceExplorer"
                    >
                      {{ $t('serviceMonitor.prototype.viewCallChain') }}
                    </button>
                  </div>
                </div>
              </div>
            </div>
          </section>
        </div>

        <div class="space-y-6">
          <!-- 右侧聚焦面板：不再堆事件列表，而是只看一个服务 -->
          <section class="rounded-3xl border border-blue-500/30 bg-[linear-gradient(180deg,rgba(23,28,42,0.98),rgba(14,17,27,0.98))] p-6 shadow-[0_0_40px_rgba(59,130,246,0.12)]">
            <div class="flex items-start justify-between gap-4">
              <div>
                <div class="text-sm uppercase tracking-[0.25em] text-blue-300/70">{{ $t('serviceMonitor.prototype.currentServiceFocus') }}</div>
                <h2 class="mt-3 text-4xl font-bold text-white">{{ selectedService.name }}</h2>
              </div>
              <span
                class="rounded-full border px-3 py-1 text-sm font-semibold uppercase tracking-wider"
                :class="riskBadgeClass(selectedService.risk)"
              >
                {{ statusText(selectedService.risk) }}
              </span>
            </div>

            <div class="mt-6 grid grid-cols-2 gap-3">
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">{{ $t('serviceMonitor.prototype.currentStatus') }}</div>
                <div class="mt-2 text-3xl font-bold" :class="riskTextClass(selectedService.risk)">
                  {{ statusText(selectedService.risk) }}
                </div>
                <div class="mt-1 text-xs text-gray-500">{{ $t('serviceMonitor.prototype.statusHint') }}</div>
              </div>
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">{{ $t('serviceMonitor.prototype.latestExceptionTime') }}</div>
                <div class="mt-2 text-3xl font-mono text-white">{{ selectedService.latestExceptionTime }}</div>
              </div>
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">{{ $t('serviceMonitor.prototype.exceptionTraceCount') }}</div>
                <div class="mt-2 text-3xl font-mono text-white">{{ selectedService.exceptionCount }}</div>
                <div class="mt-1 text-xs text-gray-500">{{ $t('serviceMonitor.prototype.dedupHint') }}</div>
              </div>
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">{{ $t('serviceMonitor.prototype.avgExceptionLatency') }}</div>
                <div class="mt-2 text-3xl font-mono text-white">{{ selectedService.avgLatencyMs }}ms</div>
                <div class="mt-1 text-xs text-gray-500">{{ $t('serviceMonitor.prototype.avgExceptionLatencyHint') }}</div>
              </div>
            </div>

            <div class="mt-6">
              <div class="text-sm uppercase tracking-[0.2em] text-gray-500">{{ $t('serviceMonitor.prototype.recentIssueSummary') }}</div>
              <ul class="mt-3 space-y-3">
                <li
                  v-if="selectedService.issues.length === 0"
                  class="rounded-2xl border border-dashed border-gray-700 bg-black/20 px-4 py-3 text-sm leading-6 text-gray-400"
                >
                  {{ $t('serviceMonitor.prototype.emptyIssues') }}
                </li>
                <li
                  v-for="issue in selectedService.issues"
                  :key="issue"
                  class="rounded-2xl border border-gray-800 bg-black/20 px-4 py-3 text-sm leading-6 text-gray-300"
                >
                  {{ issue }}
                </li>
              </ul>
            </div>

            <div class="mt-6">
              <div class="flex items-center justify-between">
                <div>
                  <div class="text-sm uppercase tracking-[0.2em] text-gray-500">{{ $t('serviceMonitor.prototype.abnormalOperations') }}</div>
                  <div class="mt-1 text-xs text-gray-500">{{ $t('serviceMonitor.prototype.abnormalOperationsHint') }}</div>
                </div>
                <div class="text-xs text-gray-500">{{ $t('serviceMonitor.prototype.currentServiceOnly') }}</div>
              </div>

              <div class="mt-3 overflow-hidden rounded-2xl border border-gray-800">
                <div class="grid grid-cols-[2fr_1fr_1fr] bg-gray-900/70 px-4 py-3 text-xs uppercase tracking-wider text-gray-500">
                  <div>{{ $t('serviceMonitor.prototype.operation') }}</div>
                  <div>{{ $t('serviceMonitor.prototype.exceptionCount') }}</div>
                  <div>{{ $t('serviceMonitor.prototype.avgLatency') }}</div>
                </div>
                <div
                  v-if="selectedService.operations.length === 0"
                  class="border-t border-gray-800 px-4 py-4 text-sm text-gray-400"
                >
                  {{ $t('serviceMonitor.prototype.emptyOperations') }}
                </div>
                <div
                  v-for="operation in selectedService.operations"
                  :key="operation.name"
                  class="grid grid-cols-[2fr_1fr_1fr] items-center border-t border-gray-800 px-4 py-3 text-sm text-gray-300"
                >
                  <div class="font-mono text-gray-200">{{ operation.name }}</div>
                  <div>{{ operation.exceptions }}</div>
                  <div>{{ operation.avgLatencyMs }}ms</div>
                </div>
              </div>
            </div>

            <div class="mt-6 flex flex-wrap gap-3">
              <button
                class="rounded-xl border border-gray-700 bg-gray-900/70 px-4 py-3 text-sm text-gray-100 hover:bg-gray-800"
                type="button"
              >
                {{ $t('serviceMonitor.prototype.latestCallChain') }}
              </button>
              <button
                class="rounded-xl bg-blue-500 px-4 py-3 text-sm font-semibold text-white hover:bg-blue-400"
                type="button"
                @click="goTraceExplorer"
              >
                {{ $t('serviceMonitor.prototype.openTracePage') }}
              </button>
            </div>
          </section>

          <!-- 右下排行：补一个全局视图，避免整个页面只有单服务 -->
          <section class="rounded-3xl border border-gray-800 bg-[linear-gradient(180deg,rgba(20,24,34,0.95),rgba(14,17,24,0.95))] p-6">
            <div class="flex items-center justify-between">
              <div>
                <h2 class="text-xl font-semibold text-white">{{ $t('serviceMonitor.prototype.rankingTitle') }}</h2>
                <div class="mt-1 text-xs text-gray-500">{{ $t('serviceMonitor.prototype.rankingHint') }}</div>
              </div>
              <span class="text-xs text-gray-500">{{ $t('serviceMonitor.prototype.global') }}</span>
            </div>

            <div class="mt-5 space-y-4">
              <div
                v-if="operationRanking.length === 0"
                class="rounded-2xl border border-dashed border-gray-700 bg-black/20 p-4 text-sm leading-7 text-gray-400"
              >
                {{ $t('serviceMonitor.prototype.emptyRanking') }}
              </div>
              <div v-for="item in operationRanking" :key="item.key">
                <div class="flex items-center justify-between text-sm">
                  <div>
                    <div class="font-mono text-gray-200">{{ item.name }}</div>
                    <div class="text-xs text-gray-500">{{ item.service }}</div>
                  </div>
                <div class="text-right">
                  <div class="font-mono text-white">{{ item.exceptions }}</div>
                  <div class="text-xs text-gray-500">{{ $t('serviceMonitor.prototype.exceptionTimes') }}</div>
                </div>
              </div>
                <div class="mt-2 h-3 overflow-hidden rounded-full bg-gray-900/80">
                  <div
                    class="h-full rounded-full bg-[linear-gradient(90deg,#f97316,#ef4444)]"
                    :style="{ width: `${(item.exceptions / maxRankingException) * 100}%` }"
                  ></div>
                </div>
              </div>
            </div>
          </section>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import dayjs from 'dayjs'
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

type RiskKind = 'critical' | 'warning' | 'healthy'

interface OperationItem {
  name: string
  exceptions: number
  avgLatencyMs: number
  highestRisk: RiskKind
}

interface TraceSampleItem {
  traceId: string
  time: string
  operation: string
  summary: string
  durationMs: number
  risk: RiskKind
}

interface ServiceItem {
  name: string
  risk: RiskKind
  exceptionCount: number
  avgLatencyMs: number
  latestExceptionTime: string
  summary: string
  issues: string[]
  operations: OperationItem[]
  recentTraces: TraceSampleItem[]
}

interface RuntimeOverview {
  abnormal_service_count: number
  abnormal_trace_count: number
  latest_exception_time_ms: number
}

interface RuntimeGlobalOperationItem {
  service_name: string
  operation_name: string
  count: number
  avg_latency_ms: number
}

interface RuntimeRecentSampleItem {
  trace_id: string
  time_ms: number
  operation_name: string
  summary: string
  duration_ms: number
  risk_level: string
}

interface RuntimeServiceOperationItem {
  operation_name: string
  count: number
  avg_latency_ms: number
}

interface RuntimeServiceItem {
  service_name: string
  risk_level: string
  exception_count: number
  avg_latency_ms: number
  latest_exception_time_ms: number
  operation_ranking: RuntimeServiceOperationItem[]
  recent_samples: RuntimeRecentSampleItem[]
}

interface ServiceRuntimeSnapshotResponse {
  overview: RuntimeOverview
  services_topk: RuntimeServiceItem[]
  global_operation_ranking: RuntimeGlobalOperationItem[]
}

const router = useRouter()
const { t } = useI18n()
const emptyService: ServiceItem = {
  name: '--',
  risk: 'healthy',
  exceptionCount: 0,
  avgLatencyMs: 0,
  latestExceptionTime: '--:--:--',
  summary: t('serviceMonitor.prototype.emptyServiceSummary'),
  issues: [],
  operations: [],
  recentTraces: []
}

const selectedServiceName = ref('')
const runtimeOverview = ref<RuntimeOverview | null>(null)
const runtimeServices = ref<RuntimeServiceItem[]>([])
const runtimeGlobalOperationRanking = ref<RuntimeGlobalOperationItem[]>([])
const runtimeRequestInFlight = ref(false)
const manualRefreshLoading = ref(false)
// 前端轮询频率跟后端 3 秒桶对齐，避免服务监控已经进窗了，
// 但页面还要再多等一轮 10 秒轮询才看见，导致答辩观感明显发钝。
const autoRefreshIntervalMs = 3_000
let autoRefreshTimer: number | null = null

// 这一刀把原型页的运行态展示彻底切成“后端真数据或空态”。
// 既然后面要验证时间窗退场，那么页面就不能再偷偷回退到 mock，否则你肉眼根本看不出退窗是否生效。
const services = computed<ServiceItem[]>(() => {
  return runtimeServices.value.map(runtimeService => {
    const runtimeRecentTraces = runtimeService.recent_samples.map(sample => ({
      traceId: sample.trace_id,
      time: formatRuntimeTime(sample.time_ms),
      operation: sample.operation_name,
      summary: sample.summary,
      durationMs: sample.duration_ms,
      risk: mapRuntimeRisk(sample.risk_level)
    }))
    const runtimeIssues = runtimeService.recent_samples
      .map(sample => sample.summary.trim())
      .filter(summary => summary.length > 0)
      .filter((summary, index, arr) => arr.indexOf(summary) === index)
      .slice(0, 3)
    const runtimeOperations = runtimeService.operation_ranking.map(operation => ({
      name: operation.operation_name,
      exceptions: operation.count,
      avgLatencyMs: operation.avg_latency_ms,
      highestRisk: mapRuntimeRisk(runtimeService.risk_level)
    }))
    const runtimeSummary = runtimeIssues[0] ?? ''

    return {
      name: runtimeService.service_name,
      risk: mapRuntimeRisk(runtimeService.risk_level),
      exceptionCount: runtimeService.exception_count,
      avgLatencyMs: runtimeService.avg_latency_ms,
      latestExceptionTime: formatRuntimeTime(runtimeService.latest_exception_time_ms),
      // 服务摘要和问题摘要当前仍然从 recent_samples 的 summary 派生，
      // 但如果后端现在没有样本，就直接显示空态，不再偷偷回退到旧 mock。
      summary: runtimeSummary || t('serviceMonitor.prototype.emptyAbnormalSummary'),
      issues: runtimeIssues,
      operations: runtimeOperations,
      // recent_samples 已经由后端按 trace_id + service 去重并按时间倒序裁成最近 3 条；
      // 这里前端只做字段名适配，不再重新发明一套筛选规则。
      recentTraces: runtimeRecentTraces
    }
  })
})

const selectedService = computed(() => {
  return services.value.find(service => service.name === selectedServiceName.value) ?? services.value[0] ?? emptyService
})

const overviewCards = computed(() => {
  const abnormalServices = runtimeOverview.value?.abnormal_service_count ?? 0
  const abnormalTraces = runtimeOverview.value?.abnormal_trace_count ?? 0
  const latestTime = runtimeOverview.value && runtimeOverview.value.latest_exception_time_ms > 0
    ? dayjs(runtimeOverview.value.latest_exception_time_ms).format('HH:mm:ss')
    : '--:--:--'

  return [
    {
      label: t('serviceMonitor.prototype.overviewAbnormalServices'),
      value: abnormalServices,
      desc: t('serviceMonitor.prototype.overviewAbnormalServicesDesc'),
      valueClass: 'text-red-400'
    },
    {
      label: t('serviceMonitor.prototype.overviewAbnormalTraces'),
      value: abnormalTraces,
      desc: t('serviceMonitor.prototype.overviewAbnormalTracesDesc'),
      valueClass: 'text-orange-400'
    },
    {
      label: t('serviceMonitor.prototype.overviewLatestException'),
      value: latestTime ?? '--:--:--',
      desc: t('serviceMonitor.prototype.overviewLatestExceptionDesc'),
      valueClass: 'text-white'
    }
  ]
})

const operationRanking = computed(() => {
  return runtimeGlobalOperationRanking.value.map(item => ({
    key: `${item.service_name}-${item.operation_name}`,
    service: item.service_name,
    name: item.operation_name,
    exceptions: item.count
  }))
})

const maxRankingException = computed(() => {
  return Math.max(...operationRanking.value.map(item => item.exceptions), 1)
})

async function fetchRuntimeSnapshot() {
  // 自动刷新和手动刷新都走同一个请求函数，但“是否正在请求”要和“按钮是否显示刷新中”拆开。
  // 否则定时器后台轮询一触发，手动按钮就会一直被锁成“刷新中”，用户会以为自己点不了。
  if (runtimeRequestInFlight.value) {
    return
  }

  runtimeRequestInFlight.value = true
  try {
    const response = await fetch('/api/service-monitor/runtime', { method: 'GET' })
    if (!response.ok) {
      throw new Error(`service-monitor/runtime failed: ${response.status}`)
    }

    const payload = (await response.json()) as ServiceRuntimeSnapshotResponse
    runtimeOverview.value = payload.overview ?? null
    runtimeServices.value = payload.services_topk ?? []
    runtimeGlobalOperationRanking.value = payload.global_operation_ranking ?? []
    if (runtimeServices.value.length > 0 &&
        !runtimeServices.value.some(item => item.service_name === selectedServiceName.value)) {
      // 左侧切成真服务榜后，当前选中项可能已经不在 top4 里。
      // 这里把选中项同步到榜单第一名，避免右侧面板继续悬着一个已经不存在的 mock 服务名。
      selectedServiceName.value = runtimeServices.value[0].service_name
    } else if (runtimeServices.value.length === 0) {
      // 时间窗退空后不再回退到 mock，所以这里也要把选中服务清空，
      // 让右侧面板老老实实进入空态，而不是继续挂着上一轮的旧服务名。
      selectedServiceName.value = ''
    }
  } catch (error) {
    console.error('Failed to fetch service runtime snapshot:', error)
  } finally {
    runtimeRequestInFlight.value = false
  }
}

async function refreshRuntimeSnapshotManually() {
  // 手动按钮只表达“这次点击触发的刷新”。
  // 自动轮询期间不再把按钮文案改成“刷新中”，避免页面一直像卡住了一样。
  if (runtimeRequestInFlight.value) {
    return
  }

  manualRefreshLoading.value = true
  try {
    await fetchRuntimeSnapshot()
  } finally {
    manualRefreshLoading.value = false
  }
}

function goTraceExplorer() {
  router.push('/traces')
}

function mapRuntimeRisk(riskLevel: string): RiskKind {
  switch (riskLevel.toLowerCase()) {
    case 'error':
      return 'critical'
    case 'warning':
      return 'warning'
    default:
      return 'healthy'
  }
}

function formatRuntimeTime(timeMs: number): string {
  if (timeMs <= 0) {
    return '--:--:--'
  }
  return dayjs(timeMs).format('HH:mm:ss')
}

function riskText(risk: RiskKind): string {
  // 这里的状态词是正式入口 /service 的可见文案，所以不能继续写死中文。
  // 风险枚举本身保持不变，只把展示层统一交给 i18n。
  return risk === 'healthy'
    ? t('serviceMonitor.prototype.statusHealthy')
    : t('serviceMonitor.prototype.statusAbnormal')
}

function riskDotClass(risk: RiskKind): string {
  switch (risk) {
    case 'critical':
      return 'bg-red-500 shadow-[0_0_12px_rgba(239,68,68,0.9)]'
    case 'warning':
      return 'bg-orange-400 shadow-[0_0_12px_rgba(251,146,60,0.8)]'
    default:
      return 'bg-green-400 shadow-[0_0_12px_rgba(74,222,128,0.8)]'
  }
}

function riskBadgeClass(risk: RiskKind): string {
  switch (risk) {
    case 'critical':
      return 'border-red-500/40 bg-red-500/10 text-red-300'
    case 'warning':
      return 'border-orange-400/40 bg-orange-400/10 text-orange-200'
    default:
      return 'border-green-500/40 bg-green-500/10 text-green-200'
  }
}

function riskTextClass(risk: RiskKind): string {
  switch (risk) {
    case 'critical':
      return 'text-red-400'
    case 'warning':
      return 'text-orange-300'
    default:
      return 'text-green-300'
  }
}

function statusText(risk: RiskKind): string {
  return riskText(risk)
}

function serviceCardClass(service: ServiceItem): string {
  const isSelected = selectedServiceName.value === service.name
  if (service.risk === 'critical') {
    return isSelected
      ? 'border-red-500/80 shadow-[0_0_28px_rgba(239,68,68,0.28)]'
      : 'border-red-500/35 hover:border-red-500/60'
  }
  if (service.risk === 'warning') {
    return isSelected
      ? 'border-orange-400/80 shadow-[0_0_28px_rgba(251,146,60,0.22)]'
      : 'border-orange-400/35 hover:border-orange-400/60'
  }
  return isSelected
    ? 'border-green-400/80 shadow-[0_0_28px_rgba(74,222,128,0.18)]'
    : 'border-gray-800 hover:border-green-400/40'
}

onMounted(() => {
  // 这个原型页现在主要用来观察时间窗进窗/退窗，所以只靠手点刷新太钝。
  // 这里改成 3 秒轮询：后端 1 秒发布一次快照、桶粒度是 3 秒，
  // 那么前端也按 3 秒拉一次，更容易在页面上直接观察到进窗和退窗。
  // 自动轮询只复用请求函数，不再去接管手动刷新按钮的 loading 文案。
  void fetchRuntimeSnapshot()
  autoRefreshTimer = window.setInterval(() => {
    void fetchRuntimeSnapshot()
  }, autoRefreshIntervalMs)
})

onBeforeUnmount(() => {
  // 页面切走后把定时器清掉，避免这个原型页已经不在前台时还继续后台拉接口。
  if (autoRefreshTimer !== null) {
    window.clearInterval(autoRefreshTimer)
    autoRefreshTimer = null
  }
})
</script>

<style scoped>
.custom-scrollbar::-webkit-scrollbar {
  width: 6px;
}

.custom-scrollbar::-webkit-scrollbar-thumb {
  background: #374151;
  border-radius: 3px;
}
</style>
