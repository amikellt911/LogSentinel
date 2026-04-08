import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import ServiceMonitorPrototype from '../views/ServiceMonitorPrototype.vue'
import Dashboard from '../views/Dashboard.vue'
import TraceExplorer from '../views/TraceExplorer.vue'
import Settings from '../views/Settings.vue'
import SettingsPrototype from '../views/SettingsPrototype.vue'

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
          path: 'traces',
          name: 'traces',
          component: TraceExplorer
        },
        {
          // 这两个页面本轮不再作为正式演示入口，但先不删文件：
          // 旧地址统一重定向到稳定页面，避免历史书签或手输 URL 直接落到半成品。
          path: 'logs',
          redirect: '/traces'
        },
        {
          path: 'benchmark',
          redirect: '/'
        },
        {
          path: 'settings',
          name: 'settings',
          component: Settings
        },
        {
          // 新设置原型页先独立挂载，避免直接覆盖旧 Settings 页面，方便并行对比。
          path: 'settings-prototype',
          name: 'settings-prototype',
          component: SettingsPrototype
        }
      ]
    }
  ]
})

export default router
