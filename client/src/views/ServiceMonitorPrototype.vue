<template>
  <div class="h-full overflow-y-auto custom-scrollbar bg-[#0b0f17] text-gray-200">
    <div class="p-6 space-y-6">
      <!-- 顶部标题区：这里只负责告诉用户这是一张“服务视角首页”，不是 Trace 检索页 -->
      <div class="flex flex-col gap-4 xl:flex-row xl:items-start xl:justify-between">
        <div>
          <h1 class="text-3xl font-bold text-white tracking-wide">服务监控原型</h1>
          <p class="text-sm text-gray-400 mt-2">
            先定位最近哪个服务有问题，再决定要不要进入 Trace 详情。
          </p>
        </div>

        <div class="flex flex-wrap items-center gap-3">
          <div class="rounded-lg border border-gray-700 bg-gray-900/70 px-3 py-2 text-sm text-gray-300">
            时间范围：最近 30 分钟
          </div>
          <button
            class="rounded-lg border border-gray-700 bg-gray-900/70 px-4 py-2 text-sm text-gray-200 hover:bg-gray-800"
            type="button"
          >
            刷新
          </button>
          <button
            class="rounded-lg bg-blue-500 px-4 py-2 text-sm font-semibold text-white hover:bg-blue-400"
            type="button"
            @click="goTraceExplorer"
          >
            进入 Trace 查询
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
                <h2 class="text-xl font-semibold text-white">服务健康概览</h2>
                <div class="mt-1 text-sm text-gray-500">这里只按服务看最近受影响的请求事件，不按 span 数直接累加。</div>
              </div>
              <span class="text-xs uppercase tracking-[0.25em] text-gray-500">Service-first</span>
            </div>

            <div class="grid grid-cols-1 gap-4 lg:grid-cols-2">
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
                      最近一小时异常链路 <span class="font-mono text-gray-200">{{ service.exceptionCount }}</span> 次
                    </div>
                    <div class="mt-1 text-xs text-gray-500">这里把一条 trace 近似视为一次请求链路，按 trace + service 去重</div>
                  </div>

                  <span
                    class="rounded-full border px-3 py-1 text-xs font-semibold uppercase tracking-wider"
                    :class="riskBadgeClass(service.risk)"
                  >
                    {{ statusText(service.risk) }}
                  </span>
                </div>

              <div class="mt-5 rounded-xl bg-black/20 p-3 text-sm">
                  <div class="text-gray-500">最近异常时间</div>
                  <div class="mt-1 text-xl font-mono text-white">{{ service.latestExceptionTime }}</div>
              </div>

                <div class="mt-4">
                  <div class="text-xs uppercase tracking-[0.2em] text-gray-500">最近异常摘要</div>
                  <div class="mt-2 min-h-[44px] text-sm leading-6 text-gray-300">
                    {{ service.summary }}
                  </div>
                </div>

                <div class="mt-4 flex items-center justify-end">
                  <span class="text-xs text-gray-500">点击查看聚焦面板</span>
                </div>
              </button>
            </div>
          </section>

          <!-- 当前服务最近异常 Trace：和左侧服务卡同列堆叠，避免被右侧大面板撑出空白 -->
          <section class="rounded-3xl border border-gray-800 bg-[linear-gradient(180deg,rgba(20,24,34,0.95),rgba(14,17,24,0.95))] p-6">
            <div class="flex items-center justify-between">
              <div>
                <h2 class="text-xl font-semibold text-white">当前服务最近异常 Trace 样本</h2>
                <div class="mt-1 text-sm text-gray-500">
                  这里只展示 {{ selectedService.name }} 的最近样本，按异常时间倒序排列，不在这里做全量搜索。
                </div>
              </div>
              <button
                class="rounded-xl border border-gray-700 bg-gray-900/70 px-4 py-2 text-sm text-gray-100 hover:bg-gray-800"
                type="button"
                @click="goTraceExplorer"
              >
                查看更多相关 Trace
              </button>
            </div>

            <div class="mt-5 space-y-4">
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
                        <div class="text-xs uppercase tracking-wider text-gray-500">Trace ID</div>
                        <div class="mt-1 break-all font-mono text-sm text-gray-200">{{ trace.traceId }}</div>
                      </div>
                      <div class="rounded-xl bg-gray-900/60 px-3 py-3">
                        <div class="text-xs uppercase tracking-wider text-gray-500">整链路耗时</div>
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
                      查看调用链
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
                <div class="text-sm uppercase tracking-[0.25em] text-blue-300/70">当前服务聚焦</div>
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
                <div class="text-sm text-gray-500">当前状态</div>
                <div class="mt-2 text-3xl font-bold" :class="riskTextClass(selectedService.risk)">
                  {{ statusText(selectedService.risk) }}
                </div>
                <div class="mt-1 text-xs text-gray-500">当前只区分“异常 / 稳定”两档</div>
              </div>
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">最近异常时间</div>
                <div class="mt-2 text-3xl font-mono text-white">{{ selectedService.latestExceptionTime }}</div>
              </div>
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">异常链路数</div>
                <div class="mt-2 text-3xl font-mono text-white">{{ selectedService.exceptionCount }}</div>
                <div class="mt-1 text-xs text-gray-500">这里把一条 trace 近似视为一次请求链路，按 trace + service 去重</div>
              </div>
              <div class="rounded-2xl bg-black/20 p-4">
                <div class="text-sm text-gray-500">异常平均耗时</div>
                <div class="mt-2 text-3xl font-mono text-white">{{ selectedService.avgLatencyMs }}ms</div>
                <div class="mt-1 text-xs text-gray-500">仅统计完整结束的异常 span</div>
              </div>
            </div>

            <div class="mt-6">
              <div class="text-sm uppercase tracking-[0.2em] text-gray-500">最近问题摘要</div>
              <ul class="mt-3 space-y-3">
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
                  <div class="text-sm uppercase tracking-[0.2em] text-gray-500">异常操作</div>
                  <div class="mt-1 text-xs text-gray-500">按 span.name 聚合；同一条 trace 内相同操作只记 1 次</div>
                </div>
                <div class="text-xs text-gray-500">只看当前服务</div>
              </div>

              <div class="mt-3 overflow-hidden rounded-2xl border border-gray-800">
                <div class="grid grid-cols-[2fr_1fr_1fr] bg-gray-900/70 px-4 py-3 text-xs uppercase tracking-wider text-gray-500">
                  <div>操作</div>
                  <div>异常链路数</div>
                  <div>异常平均耗时</div>
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
                查看最新异常调用链
              </button>
              <button
                class="rounded-xl bg-blue-500 px-4 py-3 text-sm font-semibold text-white hover:bg-blue-400"
                type="button"
                @click="goTraceExplorer"
              >
                进入 Trace 查询页
              </button>
            </div>
          </section>

          <!-- 右下排行：补一个全局视图，避免整个页面只有单服务 -->
          <section class="rounded-3xl border border-gray-800 bg-[linear-gradient(180deg,rgba(20,24,34,0.95),rgba(14,17,24,0.95))] p-6">
            <div class="flex items-center justify-between">
              <div>
                <h2 class="text-xl font-semibold text-white">异常操作排行</h2>
                <div class="mt-1 text-xs text-gray-500">全局窗口统计，按 span.name 聚合，跨服务 Top 6</div>
              </div>
              <span class="text-xs text-gray-500">Global</span>
            </div>

            <div class="mt-5 space-y-4">
              <div v-for="item in operationRanking" :key="item.key">
                <div class="flex items-center justify-between text-sm">
                  <div>
                    <div class="font-mono text-gray-200">{{ item.name }}</div>
                    <div class="text-xs text-gray-500">{{ item.service }}</div>
                  </div>
                <div class="text-right">
                  <div class="font-mono text-white">{{ item.exceptions }}</div>
                  <div class="text-xs text-gray-500">异常链路数</div>
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
import { computed, ref } from 'vue'
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

const router = useRouter()

// 这里故意把 mock 数据直接放在页面里，先验证“服务视角首页”的信息组织方式。
const services = ref<ServiceItem[]>([
  {
    name: 'order-service',
    risk: 'critical',
    exceptionCount: 45,
    avgLatencyMs: 44,
    latestExceptionTime: '10:15:30',
    summary: '订单创建链路最近频繁卡在支付确认与库存回调，超时后会触发整条请求失败。',
    issues: [
      '订单创建接口最近 30 分钟内出现多次下游超时，用户会感知到下单失败或长时间等待。',
      '支付回调确认延迟抬高后，订单状态回写经常比预期慢一拍，造成页面状态不一致。',
      '库存确认链路偶发锁竞争，导致支付成功后订单仍需等待库存服务最终回执。'
    ],
    operations: [
      { name: 'create-order', exceptions: 12, avgLatencyMs: 44, highestRisk: 'critical' },
      { name: 'pay-order', exceptions: 8, avgLatencyMs: 33, highestRisk: 'critical' },
      { name: 'query-order', exceptions: 5, avgLatencyMs: 24, highestRisk: 'warning' }
    ],
    recentTraces: [
      {
        traceId: 'trace-order-20260319-001',
        time: '10:15:30',
        operation: 'create-order',
        summary: '订单创建调用在支付确认阶段卡住，整条链路最终超时返回失败。',
        durationMs: 1440,
        risk: 'critical'
      },
      {
        traceId: 'trace-order-20260319-002',
        time: '10:12:48',
        operation: 'pay-order',
        summary: '支付成功后库存确认迟迟未回，订单状态更新明显滞后。',
        durationMs: 932,
        risk: 'warning'
      },
      {
        traceId: 'trace-order-20260319-003',
        time: '10:08:11',
        operation: 'query-order',
        summary: '订单查询接口受到上游重试影响，短时间内响应时间抬高。',
        durationMs: 615,
        risk: 'warning'
      }
    ]
  },
  {
    name: 'product-service',
    risk: 'warning',
    exceptionCount: 18,
    avgLatencyMs: 33,
    latestExceptionTime: '10:13:10',
    summary: '商品详情接口偶发慢查询，库存联查峰值时延明显升高，但整体仍可恢复。',
    issues: [
      '商品详情查询在高峰期会触发慢 SQL，用户主要感知是页面首屏加载变慢。',
      '库存联查的缓存命中率下降后，平均耗时会被数据库读取直接拉高。'
    ],
    operations: [
      { name: 'query-product-detail', exceptions: 9, avgLatencyMs: 31, highestRisk: 'warning' },
      { name: 'search-product', exceptions: 5, avgLatencyMs: 26, highestRisk: 'warning' },
      { name: 'query-product-stock', exceptions: 4, avgLatencyMs: 22, highestRisk: 'warning' }
    ],
    recentTraces: [
      {
        traceId: 'trace-product-20260319-001',
        time: '10:13:10',
        operation: 'query-product-detail',
        summary: '商品详情查询在库存联查阶段出现慢 SQL，用户首屏加载变慢。',
        durationMs: 482,
        risk: 'warning'
      },
      {
        traceId: 'trace-product-20260319-002',
        time: '10:10:42',
        operation: 'search-product',
        summary: '搜索结果聚合查询命中数据库回源，导致响应抖动。',
        durationMs: 355,
        risk: 'warning'
      },
      {
        traceId: 'trace-product-20260319-003',
        time: '10:06:29',
        operation: 'query-product-stock',
        summary: '库存信息接口在高峰流量下连续出现中等延迟，没有形成彻底失败。',
        durationMs: 298,
        risk: 'warning'
      }
    ]
  },
  {
    name: 'payment-service',
    risk: 'warning',
    exceptionCount: 12,
    avgLatencyMs: 24,
    latestExceptionTime: '10:12:02',
    summary: '支付回调成功率基本稳定，但在第三方渠道抖动时会出现短暂重试堆积。',
    issues: [
      '第三方支付网关抖动时，回调确认会出现连续重试，影响订单状态回写速度。',
      '支付通知落库路径仍然稳定，没有出现长时间积压。'
    ],
    operations: [
      { name: 'payment-callback', exceptions: 6, avgLatencyMs: 28, highestRisk: 'warning' },
      { name: 'confirm-payment', exceptions: 4, avgLatencyMs: 23, highestRisk: 'warning' },
      { name: 'refund-payment', exceptions: 2, avgLatencyMs: 21, highestRisk: 'healthy' }
    ],
    recentTraces: [
      {
        traceId: 'trace-pay-20260319-001',
        time: '10:12:02',
        operation: 'payment-callback',
        summary: '第三方渠道抖动导致支付回调重试增多，但最终仍能成功落库。',
        durationMs: 522,
        risk: 'warning'
      },
      {
        traceId: 'trace-pay-20260319-002',
        time: '10:07:37',
        operation: 'confirm-payment',
        summary: '支付确认接口在短时间内连续重试，平均耗时被明显拉高。',
        durationMs: 406,
        risk: 'warning'
      },
      {
        traceId: 'trace-pay-20260319-003',
        time: '10:03:26',
        operation: 'refund-payment',
        summary: '退款链路偶发慢响应，但没有看到大面积失败或积压。',
        durationMs: 214,
        risk: 'healthy'
      }
    ]
  },
  {
    name: 'user-service',
    risk: 'healthy',
    exceptionCount: 6,
    avgLatencyMs: 18,
    latestExceptionTime: '10:09:41',
    summary: '用户登录与鉴权整体平稳，仅有少量数据库抖动带来的瞬时慢响应。',
    issues: [
      '用户服务近期主要是少量数据库慢响应，没有形成持续故障。',
      '鉴权链路没有出现批量失败，当前更像是普通波动。'
    ],
    operations: [
      { name: 'login-user', exceptions: 3, avgLatencyMs: 18, highestRisk: 'healthy' },
      { name: 'load-user-profile', exceptions: 2, avgLatencyMs: 16, highestRisk: 'healthy' },
      { name: 'refresh-user-token', exceptions: 1, avgLatencyMs: 15, highestRisk: 'healthy' }
    ],
    recentTraces: [
      {
        traceId: 'trace-user-20260319-001',
        time: '10:09:41',
        operation: 'login-user',
        summary: '登录接口偶发慢响应，但整体成功率稳定，没有形成持续告警。',
        durationMs: 182,
        risk: 'healthy'
      },
      {
        traceId: 'trace-user-20260319-002',
        time: '10:05:14',
        operation: 'load-user-profile',
        summary: '用户资料读取受缓存回填影响，个别请求略慢于平时。',
        durationMs: 136,
        risk: 'healthy'
      },
      {
        traceId: 'trace-user-20260319-003',
        time: '10:01:52',
        operation: 'refresh-user-token',
        summary: 'token 刷新链路整体稳定，仅有极少数请求延迟上浮。',
        durationMs: 121,
        risk: 'healthy'
      }
    ]
  }
])

const selectedServiceName = ref('order-service')

// 用户点左侧卡片后，右边只看一个服务，避免页面退化成第二个 Trace 列表。
const selectedService = computed(() => {
  return services.value.find(service => service.name === selectedServiceName.value) ?? services.value[0]
})

const overviewCards = computed(() => {
  const abnormalServices = services.value.filter(item => item.risk !== 'healthy').length
  const highRiskEvents = services.value.reduce((sum, item) => sum + item.exceptionCount, 0)
  const latestTime = services.value
    .map(item => item.latestExceptionTime)
    .sort()
    .reverse()[0]

  return [
    {
      label: '异常服务数',
      value: abnormalServices,
      desc: '当前时间窗口内至少出现过 1 个 Error span 的服务数',
      valueClass: 'text-red-400'
    },
    {
      label: '异常链路数',
      value: highRiskEvents,
      desc: '这里把一条 trace 近似视为一次请求链路，按 trace + service 去重',
      valueClass: 'text-orange-400'
    },
    {
      label: '最近异常时间',
      value: latestTime ?? '--:--:--',
      desc: '最近一个异常 span 的确认时间，优先取 end_time',
      valueClass: 'text-white'
    }
  ]
})

const operationRanking = computed(() => {
  return services.value
    .flatMap(service =>
      service.operations.map(operation => ({
        key: `${service.name}-${operation.name}`,
        service: service.name,
        name: operation.name,
        exceptions: operation.exceptions
      }))
    )
    .sort((a, b) => b.exceptions - a.exceptions)
    .slice(0, 6)
})

const maxRankingException = computed(() => {
  return Math.max(...operationRanking.value.map(item => item.exceptions), 1)
})

function goTraceExplorer() {
  router.push('/traces')
}

function riskText(risk: RiskKind): string {
  return risk === 'healthy' ? '稳定' : '异常'
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
