import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import './style.css'
import App from './App.vue'
import router from './router'
import i18n from './i18n'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'

async function bootstrap() {
  // 入口先预取一次 app_language，再挂载整棵应用。
  // 否则当前语言只会在某个设置页自己加载后才切过去，用户第一次打开根路由时会先看到错误语言。
  try {
    const response = await fetch('/api/settings/all')
    if (response.ok) {
      const data = await response.json()
      const language = data?.config?.app_language
      if (language === 'zh' || language === 'en') {
        // @ts-ignore
        i18n.global.locale.value = language
      }
    }
  } catch (error) {
    console.warn('Failed to preload app language, fallback to default locale:', error)
  }

  const app = createApp(App)

  app.use(i18n)

  // Register Icons
  for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
    app.component(key, component)
  }

  app.use(createPinia())
  app.use(router)
  app.use(ElementPlus, { size: 'small', zIndex: 3000 }) // 'small' is more tech/dense

  app.mount('#app')
}

void bootstrap()
