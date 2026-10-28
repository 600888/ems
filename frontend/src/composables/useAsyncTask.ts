import { ref } from 'vue'
import { errorMessage } from '@/api/http'

/** 单次操作状态；错误保留给就近的表单或重试入口，不自动重放写请求。 */
export function useAsyncTask() {
  const busy = ref(false)
  const error = ref('')
  async function run<T>(operation: () => Promise<T>): Promise<T | undefined> {
    if (busy.value) return
    busy.value = true
    error.value = ''
    try {
      return await operation()
    } catch (cause) {
      error.value = errorMessage(cause)
      return undefined
    } finally {
      busy.value = false
    }
  }
  return { busy, error, run }
}
