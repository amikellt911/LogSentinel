import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import Dashboard from '../views/Dashboard.vue'
import LiveLogs from '../views/LiveLogs.vue'
import History from '../views/History.vue'
import BatchInsights from '../views/BatchInsights.vue'
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
          path: 'logs',
          name: 'logs',
          component: LiveLogs
        },
        {
          path: 'history',
          name: 'history',
          component: History
        },
        {
          path: 'insights',
          name: 'insights',
          component: BatchInsights
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
