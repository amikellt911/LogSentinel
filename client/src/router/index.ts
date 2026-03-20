import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import ServiceMonitor from '../views/ServiceMonitor.vue'
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
          component: ServiceMonitor
        },
        {
          path: 'service-prototype',
          name: 'service-prototype',
          component: ServiceMonitorPrototype
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
