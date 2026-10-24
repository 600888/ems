import {onBeforeUnmount, onMounted, ref} from 'vue'

/** 顶栏时钟：设计图为 YYYY-MM-DD  HH:mm:ss（中间两个空格） */
export function useClock() {
  const text = ref(format(new Date()))
  let timer = 0

  function format(date: Date) {
    const pad = (value: number) => String(value).padStart(2, '0')
    const day = `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`
    const time = `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`
    return `${day}  ${time}`
  }

  onMounted(() => {
    timer = window.setInterval(() => {
      text.value = format(new Date())
    }, 1000)
  })

  onBeforeUnmount(() => window.clearInterval(timer))

  return {text}
}
