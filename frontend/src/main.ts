import {createApp, type Component} from 'vue'
import {createPinia} from 'pinia'
import {
  ElCheckbox,
  ElConfigProvider,
  ElDrawer,
  ElDropdown,
  ElForm,
  ElFormItem,
  ElInput,
  ElOption,
  ElPagination,
  ElSelect,
  ElSkeleton,
  ElSwitch,
  ElTooltip,
  ElTree,
} from 'element-plus'

import './styles/main.css'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import './styles/element.css'

import App from './App.vue'
import router from './router'

/*
 * 按需注册 Element Plus 组件：不使用 app.use(ElementPlus)，
 * 避免把整库打进首屏 chunk。新增组件时在此追加即可。
 */
const components: Record<string, Component> = {
  ElCheckbox,
  ElConfigProvider,
  ElDrawer,
  ElDropdown,
  ElForm,
  ElFormItem,
  ElInput,
  ElOption,
  ElPagination,
  ElSelect,
  ElSkeleton,
  ElSwitch,
  ElTooltip,
  ElTree,
}

const app = createApp(App).use(createPinia()).use(router)
Object.entries(components).forEach(([name, component]) => app.component(name, component))
app.mount('#app')
