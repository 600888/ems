import { computed, reactive, ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { usersApi } from '@/api/identity'
import { errorMessage } from '@/api/http'
import { useAuthStore } from './auth'
import type { ManagedUser, UserCapabilities, UserListQuery } from '@/types/identity'

export const useUsersStore = defineStore('users', () => {
  const auth = useAuthStore()
  const query = reactive<UserListQuery>({
    keyword: '',
    role_code: '',
    status: '',
    page: 1,
    page_size: 20,
  })
  const rows = ref<ManagedUser[]>([])
  const total = ref(0)
  const stats = ref({ total: 0, enabled: 0, disabled: 0, locked: 0 })
  const busy = ref(false),
    error = ref(''),
    capabilityError = ref('')
  const capabilities = ref<UserCapabilities | null>(null)
  const tree = computed(() => capabilities.value?.scope_tree ?? [])
  let owner = '',
    sequence = 0
  let request: AbortController | undefined
  watch([() => auth.user?.user_id, () => auth.can('user.read')], () => {
    sequence++
    request?.abort()
    owner = ''
    rows.value = []
    total.value = 0
    capabilities.value = null
    stats.value = { total: 0, enabled: 0, disabled: 0, locked: 0 }
    busy.value = false
    error.value = ''
    capabilityError.value = ''
  })
  async function load() {
    const id = ++sequence
    request?.abort()
    request = new AbortController()
    busy.value = true
    error.value = ''
    try {
      const result = await usersApi.list({ ...query }, request.signal)
      if (id !== sequence) return
      rows.value = result.items
      total.value = result.total
      stats.value = result.stats
    } catch (cause) {
      if (id === sequence) error.value = errorMessage(cause)
    } finally {
      if (id === sequence) busy.value = false
    }
  }
  async function loadCapabilities(force = false) {
    if (capabilities.value && owner === auth.user?.user_id && !force) return
    capabilityError.value = ''
    const userId = auth.user?.user_id
    try {
      const value = await usersApi.capabilities()
      if (auth.user?.user_id === userId && auth.can('user.read')) {
        capabilities.value = value
        owner = userId ?? ''
      }
    } catch (cause) {
      capabilityError.value = errorMessage(cause)
      throw cause
    }
  }
  function reset() {
    query.keyword = ''
    query.role_code = ''
    query.status = ''
    query.page = 1
    return load()
  }
  return {
    query,
    rows,
    total,
    stats,
    busy,
    error,
    capabilityError,
    capabilities,
    tree,
    load,
    loadCapabilities,
    reset,
  }
})
