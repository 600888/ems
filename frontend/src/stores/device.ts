import { computed, ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { deviceApi, clearEngineeringSession } from '@/api/device'
import { errorMessage } from '@/api/http'
import { useAuthStore } from './auth'
import type { DeviceNode } from '@/types/domain'
import type { EngineeringRecord } from '@/types/engineering'

export const useDeviceStore = defineStore('device', () => {
  const auth = useAuthStore()
  const tree = ref<DeviceNode[]>([])
  const rows = ref<EngineeringRecord[]>([])
  const current = ref<EngineeringRecord | null>(null)
  const loading = ref(false),
    error = ref(''),
    revision = ref(''),
    stationId = ref('')
  const currentId = ref(''),
    expandedIds = ref<string[]>([]),
    createDrawerOpen = ref(false)
  const keyword = ref(''),
    page = ref(1),
    total = ref(0),
    pageSize = 20
  let epoch = 0
  let pending: Promise<void> | undefined
  const currentRow = computed(() => current.value)
  const parent = computed(() =>
    current.value?.node_kind === 'device'
      ? current.value.parent_device_id
      : currentId.value || stationId.value,
  )
  watch(
    () => auth.user?.user_id,
    () => {
      epoch++
      pending = undefined
      tree.value = []
      rows.value = []
      current.value = null
      currentId.value = ''
      stationId.value = ''
      revision.value = ''
      error.value = ''
      loading.value = false
      clearEngineeringSession()
    },
  )
  async function load() {
    if (pending) return pending
    const owner = epoch
    loading.value = true
    error.value = ''
    const operation = (async () => {
      try {
        const config = await deviceApi.configuration()
        const nextTree = await deviceApi.tree()
        const result = await deviceApi.list(
          keyword.value ? { keyword: keyword.value } : {},
          page.value,
          pageSize,
        )
        if (owner !== epoch) return
        tree.value = nextTree
        rows.value = result.items
        total.value = result.total
        stationId.value = config.station_device_id
        revision.value = config.config_revision
        expandedIds.value = nextTree.map((n) => n.id)
        if (!currentId.value) currentId.value = config.station_device_id
      } catch (cause) {
        if (owner === epoch) error.value = errorMessage(cause)
      } finally {
        if (owner === epoch) loading.value = false
      }
    })()
    pending = operation
    try {
      await operation
    } finally {
      if (pending === operation) pending = undefined
    }
  }
  async function selectNode(id: string) {
    currentId.value = id
    const owner = epoch
    try {
      const value = await deviceApi.detail(id)
      if (owner === epoch && currentId.value === id) current.value = value
    } catch (cause) {
      if (owner === epoch) error.value = errorMessage(cause)
    }
  }
  return {
    tree,
    rows,
    current,
    currentRow,
    loading,
    error,
    revision,
    stationId,
    currentId,
    parent,
    expandedIds,
    createDrawerOpen,
    keyword,
    page,
    pageSize,
    total,
    load,
    selectNode,
  }
})
