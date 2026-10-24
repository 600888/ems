import {defineStore} from 'pinia'
import {ref} from 'vue'

export type ThemeMode = 'dark' | 'light'

const STORAGE_KEY = 'ems-theme'

/** 明暗主题：深色为设计图基准，默认深色，选择写入 localStorage */
export const useThemeStore = defineStore('theme', () => {
  const stored = localStorage.getItem(STORAGE_KEY)
  // 支持 ?theme=light / ?theme=dark 直达，便于截图验收与分享
  const param = new URLSearchParams(window.location.search).get('theme')
  const mode = ref<ThemeMode>(
    param === 'light' ? 'light' : param === 'dark' ? 'dark' : stored === 'light' ? 'light' : 'dark',
  )

  /** 把主题类同步到 html 上（Element Plus 深色变量依赖 html.dark） */
  function apply() {
    const root = document.documentElement
    root.classList.toggle('dark', mode.value === 'dark')
    root.classList.toggle('light', mode.value === 'light')
    localStorage.setItem(STORAGE_KEY, mode.value)
  }

  function setMode(next: ThemeMode) {
    mode.value = next
    apply()
  }

  function toggle() {
    setMode(mode.value === 'dark' ? 'light' : 'dark')
  }

  return {mode, apply, setMode, toggle}
})
