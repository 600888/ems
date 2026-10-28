import { computed, ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { useAuthStore } from '@/stores/auth'
import { scopedDeviceTree } from '@/domain/resource-scope'

// 授权选择和运行导航共用设备目录，确保设备的父子关系一致。
import { adminTree as monitorTree } from '@/mocks/devices'
import {
  monitorProfiles,
  pcsYcPoints,
  pcsYxSignals,
  ycPoints,
  ykCommands,
  yxSignals,
  ytPoints,
  type DeviceProfile,
} from '@/mocks/points'
import type {
  ControlLog,
  DeviceNode,
  Metric,
  YcPoint,
  YkCommand,
  YtPoint,
  YxSignal,
} from '@/types/domain'

export type MonitorScope = 'all' | 'alarm' | 'offline'

function findNode(nodes: DeviceNode[], id: string): DeviceNode | undefined {
  for (const node of nodes) {
    if (node.id === id) return node
    const hit = node.children && findNode(node.children, id)
    if (hit) return hit
  }
  return undefined
}

/** 未在示例档案中登记的设备：给出空能力档案，页面按「暂无测点」呈现 */
function emptyProfile(node: DeviceNode | undefined): DeviceProfile {
  const blankMetrics: Metric[] = ['实时数据', '通信状态', '更新时刻'].map((label, index) => ({
    label,
    value: '—',
    note: index === 0 ? '该设备暂无实时点表' : '示例数据未覆盖',
    tone: 'ink' as const,
  }))
  return {
    id: node?.id ?? 'unknown',
    name: node?.name ?? '未知设备',
    code: node?.code ?? '—',
    type: node?.kind === 'component' ? '层级节点' : '设备',
    vendor: '示例厂商 · 待接入',
    firmware: '—',
    parentPath: `华东示范储能站  /  储能单元 01  /  ${node?.name ?? '未知设备'}`,
    pathLabel: '储能单元 01',
    icon: node?.icon ?? 'hard-drive',
    dataSource: node?.name ?? '—',
    status: node?.status === 'alarm' ? 'alarm' : node?.status === 'offline' ? 'offline' : 'online',
    metrics: blankMetrics,
    tabCounts: { telemetry: 0, signal: 0, control: 0, setpoint: 0 },
  }
}

/** 前台设备监视：设备树、实时档案、四遥数据、运行操作 */
export const useMonitorStore = defineStore('monitor', () => {
  const auth = useAuthStore()
  const authorized = computed(() =>
    scopedDeviceTree(
      monitorTree,
      auth.full ? auth.session?.scope_mode : undefined,
      auth.full ? (auth.session?.scopes ?? []) : [],
    ),
  )
  const tree = computed(() => authorized.value.tree)
  const selectedId = ref('bms01')
  const keyword = ref('')
  const scope = ref<MonitorScope>('all')
  const loading = ref(false)
  const error = ref('')
  const paused = ref(false)
  const lastUpdated = ref('14:32:08')

  const selectedYcCodes = ref<string[]>(['soc', 'cell_temp_max', 'cell_temp_min'])
  const curveWindow = ref('最近 30 分钟')
  const sampleInterval = ref('2 秒')

  const ytRows = ref<YtPoint[]>(ytPoints.map((row) => ({ ...row })))
  const controlLogRows = ref<ControlLog[]>([])
  const activeCommand = ref<YkCommand | null>(null)
  const controlStep = ref<1 | 2 | 3>(1)
  const controlReason = ref('计划维护停机')
  const controlRunning = ref(false)
  const controlResult = ref<'idle' | 'success' | 'failed' | 'timeout'>('idle')

  const selectedNode = computed(() => findNode(tree.value, selectedId.value))
  const profile = computed<DeviceProfile>(() =>
    authorized.value.allowedIds.has(selectedId.value)
      ? (monitorProfiles[selectedId.value] ?? emptyProfile(selectedNode.value))
      : emptyProfile(undefined),
  )

  const isBms = computed(
    () => authorized.value.allowedIds.has(selectedId.value) && selectedId.value === 'bms01',
  )
  const ycList = computed<YcPoint[]>(() =>
    isBms.value
      ? ycPoints
      : authorized.value.allowedIds.has(selectedId.value) && selectedId.value === 'pcs01'
        ? pcsYcPoints
        : [],
  )
  const yxList = computed<YxSignal[]>(() =>
    isBms.value
      ? yxSignals
      : authorized.value.allowedIds.has(selectedId.value) && selectedId.value === 'pcs01'
        ? pcsYxSignals
        : [],
  )
  const ykList = computed<YkCommand[]>(() =>
    auth.can('command.yk') && profile.value.tabCounts.control > 0 ? ykCommands : [],
  )
  const ytList = computed<YtPoint[]>(() =>
    auth.can('command.yt') && profile.value.tabCounts.setpoint > 0 ? ytRows.value : [],
  )

  /** 树过滤：按状态范围 + 关键字，保留命中节点的祖先链 */
  const filteredTree = computed<DeviceNode[]>(() => {
    const kw = keyword.value.trim().toLowerCase()
    if (scope.value === 'all' && !kw) return tree.value

    const keep = (node: DeviceNode): DeviceNode | null => {
      const children = (node.children ?? [])
        .map(keep)
        .filter((child): child is DeviceNode => Boolean(child))
      const hitStatus =
        scope.value === 'all' ||
        (scope.value === 'alarm' ? node.status === 'alarm' : node.status === 'offline')
      const hitKeyword =
        !kw || node.name.toLowerCase().includes(kw) || node.code.toLowerCase().includes(kw)
      if ((hitStatus && hitKeyword) || children.length > 0) return { ...node, children }
      return null
    }

    return tree.value.map(keep).filter((node): node is DeviceNode => Boolean(node))
  })

  const counts = computed(() => {
    const devices: DeviceNode[] = []
    const visit = (nodes: DeviceNode[]) => {
      for (const node of nodes) {
        if (node.isDevice) devices.push(node)
        visit(node.children ?? [])
      }
    }
    visit(tree.value)
    return {
      all: devices.length,
      alarm: devices.filter((n) => n.status === 'alarm').length,
      offline: devices.filter((n) => n.status === 'offline').length,
    }
  })

  watch(
    () =>
      JSON.stringify([
        auth.user?.user_id,
        auth.session?.scope_mode,
        auth.session?.scopes,
        auth.session?.permissions,
      ]),
    () => {
      function firstDevice(nodes: DeviceNode[]): string | undefined {
        for (const node of nodes) {
          if (node.isDevice) return node.id
          const id = firstDevice(node.children ?? [])
          if (id) return id
        }
      }
      selectedId.value = authorized.value.allowedIds.has('bms01')
        ? 'bms01'
        : (firstDevice(tree.value) ?? '')
      keyword.value = ''
      scope.value = 'all'
      selectedYcCodes.value = ['soc', 'cell_temp_max', 'cell_temp_min']
      activeCommand.value = null
      controlRunning.value = false
      controlResult.value = 'idle'
      controlStep.value = 1
      controlReason.value = ''
      ytRows.value = ytPoints.map((row) => ({ ...row }))
      controlLogRows.value = []
    },
    { immediate: true },
  )

  /** 选中测点的曲线数据（最多 6 个） */
  const curveSeries = computed(() =>
    selectedYcCodes.value
      .map((code) => ycList.value.find((point) => point.code === code))
      .filter((point): point is YcPoint => Boolean(point)),
  )

  function selectDevice(id: string) {
    if (!authorized.value.allowedIds.has(id) || !findNode(tree.value, id)?.isDevice) return
    selectedId.value = id
    controlRunning.value = false
    controlResult.value = 'idle'
    controlStep.value = 1
    activeCommand.value = null
    ytRows.value = ytPoints.map((row) => ({ ...row }))
  }

  function toggleYc(code: string) {
    if (selectedYcCodes.value.includes(code)) {
      selectedYcCodes.value = selectedYcCodes.value.filter((item) => item !== code)
      return
    }
    if (selectedYcCodes.value.length >= 6) return
    selectedYcCodes.value = [...selectedYcCodes.value, code]
  }

  function openCommand(command: YkCommand) {
    if (!auth.can('command.yk') || !ykList.value.includes(command) || command.disabledReason) return
    activeCommand.value = command
    controlStep.value = 1
    controlResult.value = 'idle'
  }

  function closeCommand() {
    controlRunning.value = false
    activeCommand.value = null
    controlStep.value = 1
    controlResult.value = 'idle'
  }

  /** 模拟指令下发：发送中 → 设备反馈 */
  function executeCommand() {
    if (
      !auth.can('command.yk') ||
      !activeCommand.value ||
      controlRunning.value ||
      !authorized.value.allowedIds.has(selectedId.value)
    )
      return
    const command = activeCommand.value
    const deviceId = selectedId.value
    const userId = auth.user?.user_id
    controlStep.value = 3
    controlRunning.value = true
    controlResult.value = 'idle'
    setTimeout(() => {
      if (
        auth.user?.user_id !== userId ||
        selectedId.value !== deviceId ||
        activeCommand.value !== command ||
        !auth.can('command.yk') ||
        !authorized.value.allowedIds.has(deviceId)
      )
        return
      controlRunning.value = false
      controlResult.value = 'success'
      controlLogRows.value = [
        {
          time: new Date().toLocaleTimeString('zh-CN', { hour12: false }),
          command: command.name,
          target: command.action,
          operator: auth.user?.display_name ?? '',
          result: 'success',
        },
        ...controlLogRows.value,
      ]
    }, 1200)
  }

  function setYtValue(name: string, value: string) {
    if (!auth.can('command.yt') || !ytList.value.length) return
    ytRows.value = ytRows.value.map((row) =>
      row.name === name ? { ...row, modified: value } : row,
    )
  }

  function resetYtValue(name: string) {
    if (!auth.can('command.yt') || !ytList.value.length) return
    ytRows.value = ytRows.value.map((row) =>
      row.name === name ? { ...row, modified: row.current } : row,
    )
  }

  /** 2 秒一次的心跳：只推进更新时间，数值保持示例稳定 */
  function tick() {
    if (paused.value) return
    lastUpdated.value = new Date().toLocaleTimeString('zh-CN', { hour12: false })
  }

  async function load() {
    loading.value = true
    error.value = ''
    await new Promise((done) => setTimeout(done, 120))
    loading.value = false
  }

  return {
    tree,
    selectedId,
    keyword,
    scope,
    loading,
    error,
    paused,
    lastUpdated,
    selectedYcCodes,
    curveWindow,
    sampleInterval,
    ytRows,
    controlLogRows,
    activeCommand,
    controlStep,
    controlReason,
    controlRunning,
    controlResult,
    selectedNode,
    profile,
    ycList,
    yxList,
    ykList,
    ytList,
    filteredTree,
    counts,
    curveSeries,
    selectDevice,
    toggleYc,
    openCommand,
    closeCommand,
    executeCommand,
    setYtValue,
    resetYtValue,
    tick,
    load,
  }
})
