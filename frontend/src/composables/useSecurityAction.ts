import { reactive } from 'vue'
import { identityApi } from '@/api/identity'
import { ApiError, errorMessage } from '@/api/http'

export interface SecurityPrompt {
  title: string
  target?: string
  message: string
  confirmLabel?: string
  destructive?: boolean
  password?: boolean
  temporaryPassword?: boolean
  username?: string
  minimum?: number
}
export function useSecurityAction(refresh?: () => Promise<unknown>) {
  const state = reactive({
    open: false,
    busy: false,
    error: '',
    blocked: false,
    prompt: null as SecurityPrompt | null,
  })
  let operation: ((temporary: string) => Promise<unknown>) | undefined
  function request(prompt: SecurityPrompt, action: (temporary: string) => Promise<unknown>) {
    state.prompt = prompt
    state.open = true
    state.error = ''
    state.blocked = false
    operation = action
  }
  async function confirm(password: string, temporary: string) {
    if (state.busy || state.blocked || !operation) return
    state.busy = true
    state.error = ''
    try {
      if (state.prompt?.password !== false) await identityApi.reauthenticate(password)
      await operation(temporary)
      state.open = false
      operation = undefined
      await refresh?.()
    } catch (error) {
      state.blocked =
        error instanceof ApiError && (error.status === 409 || error.effect === 'Unknown')
      state.error =
        errorMessage(error) +
        (error instanceof ApiError && error.effect === 'Unknown'
          ? '。结果未确认，请关闭窗口并刷新核对，不要重复提交。'
          : '')
    } finally {
      state.busy = false
    }
  }
  async function cancel() {
    if (state.busy) return
    const reload = state.blocked
    state.open = false
    state.error = ''
    operation = undefined
    if (reload) await refresh?.()
  }
  return { state, request, confirm, cancel }
}
