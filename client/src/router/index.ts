import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import ServiceMonitor from '../views/ServiceMonitor.vue'
import Dashboard from '../views/Dashboard.vue'
import LiveLogs from '../views/LiveLogs.vue'
import TraceExplorer from '../views/TraceExplorer.vue'
import BatchInsights from '../views/BatchInsights.vue'
import AIEngine from '../views/AIEngine.vue'
import SystemStatus from '../views/SystemStatus.vue'
import Benchmark from '../views/Benchmark.vue'
import Settings from '../views/Settings.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      component: MainLayout,
      redirect: '/service',
      children: [
        {
          path: 'service',
          name: 'service',
          component: ServiceMonitor
        },
        {
          path: 'system',
          name: 'system',
          component: SystemStatus
        },
        {
          path: 'logs',
          name: 'logs',
          component: LiveLogs
        },
        {
          path: 'traces',
          name: 'traces',
          component: TraceExplorer
        },
        {
          path: 'ai-engine',
          name: 'ai-engine',
          component: AIEngine
        },
        {
          path: 'benchmark',
          name: 'benchmark',
          component: Benchmark
        },
        {
          path: 'settings',
          name: 'settings',
          component: Settings
        },
        // 保留旧路由以防万一
        {
            path: 'dashboard-legacy',
            name: 'dashboard-legacy',
            component: Dashboard
        },
        {
            path: 'insights-legacy',
            name: 'insights-legacy',
            component: BatchInsights
        }
      ]
    }
  ]
})

export default router
