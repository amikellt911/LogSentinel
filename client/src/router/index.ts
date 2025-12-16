import { createRouter, createWebHistory } from 'vue-router'
import MainLayout from '../layout/MainLayout.vue'
import Dashboard from '../views/Dashboard.vue'
import LiveLogs from '../views/LiveLogs.vue'
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
          path: 'settings',
          name: 'settings',
          component: Settings
        }
      ]
    }
  ]
})

export default router
