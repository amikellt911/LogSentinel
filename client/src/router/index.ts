import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import ServiceMonitorPrototype from '../views/ServiceMonitorPrototype.vue'
import Dashboard from '../views/Dashboard.vue'
import LiveLogs from '../views/LiveLogs.vue'
import TraceExplorer from '../views/TraceExplorer.vue'
import Benchmark from '../views/Benchmark.vue'
import Settings from '../views/Settings.vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      component: MainLayout,
      children: [
        {
          path: '',
          name: 'dashboard',
          component: Dashboard
        },
        {
          path: 'service',
          name: 'service',
          // 服务监控原型已经完成本轮验收，所以正式入口 /service 直接切到新页面。
          // 旧 ServiceMonitor.vue 先不再暴露路由入口，避免用户继续点到老 mock 页面。
          component: ServiceMonitorPrototype
        },
        {
          // 兼容之前联调时保留下来的旧地址，统一重定向到正式入口。
          path: 'service-prototype',
          redirect: '/service'
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
          path: 'benchmark',
          name: 'benchmark',
          component: Benchmark
        },
        {
          path: 'settings',
          name: 'settings',
          component: Settings
        }
      ]
    }
  ]
})

export default router
