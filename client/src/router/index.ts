import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import ServiceMonitorPrototype from '../views/ServiceMonitorPrototype.vue'
import Dashboard from '../views/Dashboard.vue'
import TraceExplorer from '../views/TraceExplorer.vue'
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
          // 设置原型这条线已经完成当前阶段验收，所以 /settings 直接收口到真实页面。
          // 旧 Settings.vue 先不再暴露正式入口，避免用户继续在两个设置页之间来回跳。
          path: 'settings',
          name: 'settings',
          component: SettingsPrototype
        },
        {
          // 兼容之前联调阶段保留下来的旧地址：
          // 现在统一回到正式设置入口，不再继续维持“双设置页”语义。
          path: 'settings-prototype',
          redirect: '/settings'
        }
      ]
    }
  ]
})

export default router
