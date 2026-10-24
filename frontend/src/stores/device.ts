import {computed, ref} from 'vue'
import {defineStore} from 'pinia'

import {deviceApi} from '@/api/device'
import type {DeviceNode, DeviceRow} from '@/types/domain'

const ALL_TYPE = '全部类型'
const ALL_CONFIG = '全部配置状态'

/** 后台设备管理：层级树、列表筛选、展开状态（画板 01 / 07） */
export const useDeviceStore = defineStore('device', () => {
  const tree = ref<DeviceNode[]>([])
  const rows = ref<DeviceRow[]>([])
  const loading = ref(false)
  const error = ref('')

  const keyword = ref('')
  const typeFilter = ref(ALL_TYPE)
  const configFilter = ref(ALL_CONFIG)
  const selectedIds = ref<string[]>([])
  const page = ref(1)
  const pageSize = ref(6)

  const expandedIds = ref<string[]>(['ess000001', 'unit01', 'battery01', 'rack01', 'pack01', 'station-common'])
  const currentId = ref('pcs01')
  const createDrawerOpen = ref(false)

  const typeOptions = [ALL_TYPE, 'PCS', 'BMS', '层级', '空调', '消防', '环控']
  /** 后台只看配置状态，运行状态在设备监视查看 */
  const configOptions = [ALL_CONFIG, '已生效', '有草稿']

  const filtered = computed(() =>
    rows.value.filter((row) => {
      const hitKeyword =
        !keyword.value ||
        row.name.includes(keyword.value) ||
        row.code.includes(keyword.value) ||
        row.vendor.includes(keyword.value)
      const hitType = typeFilter.value === ALL_TYPE || row.type === typeFilter.value
      const hitConfig =
        configFilter.value === ALL_CONFIG ||
        (configFilter.value === '已生效' ? row.versionState === 'applied' : row.versionState === 'draft')
      return hitKeyword && hitType && hitConfig
    }),
  )

  const total = computed(() =>
    keyword.value || typeFilter.value !== ALL_TYPE || configFilter.value !== ALL_CONFIG
      ? filtered.value.length
      : 12,
  )

  const paged = computed(() => {
    const start = (page.value - 1) * pageSize.value
    return filtered.value.slice(start, start + pageSize.value)
  })

  const currentRow = computed(() => rows.value.find((row) => row.id === currentId.value) ?? rows.value[0])

  /** 读取设备树与列表，失败时保留页面框架并给出重试入口 */
  async function load() {
    loading.value = true
    error.value = ''
    try {
      const [nextTree, nextRows] = await Promise.all([deviceApi.tree(), deviceApi.list()])
      tree.value = nextTree
      rows.value = nextRows
    } catch {
      error.value = '连接暂时不可用，请重试获取设备列表。'
    } finally {
      loading.value = false
    }
  }

  function isExpanded(id: string) {
    return expandedIds.value.includes(id)
  }

  function toggleExpand(id: string) {
    expandedIds.value = isExpanded(id)
      ? expandedIds.value.filter((item) => item !== id)
      : [...expandedIds.value, id]
  }

  function selectNode(id: string) {
    currentId.value = id
  }

  function toggleRow(id: string) {
    selectedIds.value = selectedIds.value.includes(id)
      ? selectedIds.value.filter((item) => item !== id)
      : [...selectedIds.value, id]
  }

  function toggleAll() {
    selectedIds.value = selectedIds.value.length === paged.value.length ? [] : paged.value.map((row) => row.id)
  }

  function setKeyword(value: string) {
    keyword.value = value
    page.value = 1
  }

  function setType(value: string) {
    typeFilter.value = value
    page.value = 1
  }

  function setConfigState(value: string) {
    configFilter.value = value
    page.value = 1
  }

  return {
    tree,
    rows,
    loading,
    error,
    keyword,
    typeFilter,
    configFilter,
    selectedIds,
    page,
    pageSize,
    expandedIds,
    currentId,
    createDrawerOpen,
    typeOptions,
    configOptions,
    filtered,
    total,
    paged,
    currentRow,
    load,
    isExpanded,
    toggleExpand,
    selectNode,
    toggleRow,
    toggleAll,
    setKeyword,
    setType,
    setConfigState,
  }
})
