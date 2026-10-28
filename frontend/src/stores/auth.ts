import { computed, ref } from 'vue'
import { defineStore } from 'pinia'
import { identityApi } from '@/api/identity'
import { ApiError, setCsrfToken } from '@/api/http'
import { roleLabel } from '@/domain/identity'
import type { AuthSession } from '@/types/identity'

export const useAuthStore = defineStore('auth', () => {
  const session = ref<AuthSession | null>(null)
  const initialized = ref(false)
  const endReason = ref('')
  let restoring: Promise<void> | undefined
  let generation = 0
  const user = computed(() => session.value?.user)
  const full = computed(() => session.value?.kind === 'full')
  const displayName = computed(() =>
    user.value ? `${user.value.display_name} · ${roleLabel(user.value.role_code)}` : '',
  )
  const can = (permission: string) =>
    full.value && Boolean(session.value?.permissions?.includes(permission))
  const landing = computed(() =>
    session.value?.scope_mode === 'all' || session.value?.scopes?.length
      ? '/monitor/overview'
      : '/profile',
  )

  function apply(value: AuthSession) {
    session.value = value
    initialized.value = true
    endReason.value = ''
    setCsrfToken(value.csrf_token)
  }
  function clear(reason = '') {
    generation++
    session.value = null
    initialized.value = true
    endReason.value = reason
    setCsrfToken('')
  }
  async function restore(force = false) {
    if (restoring) return restoring
    if (initialized.value && !force) return
    const expected = generation
    restoring = (async () => {
      try {
        const value = await identityApi.session()
        if (generation === expected) apply(value)
      } catch (error) {
        if (generation !== expected) return
        if (error instanceof ApiError && error.status === 401) clear()
        else throw error
      } finally {
        restoring = undefined
      }
    })()
    return restoring
  }
  async function login(username: string, password: string) {
    const expected = ++generation
    const value = await identityApi.login(username, password)
    if (generation === expected) apply(value)
  }
  async function logout(all = false) {
    if (all) await identityApi.logoutAll()
    else await identityApi.logout()
    clear('logout')
    if ('BroadcastChannel' in window) {
      const channel = new BroadcastChannel('ems-session')
      channel.postMessage('logout')
      channel.close()
    }
  }
  return {
    session,
    user,
    full,
    initialized,
    endReason,
    displayName,
    landing,
    can,
    apply,
    clear,
    restore,
    login,
    logout,
  }
})
