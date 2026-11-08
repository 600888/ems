import { computed, ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { useAuthStore } from './auth'
import { deviceApi, stableId, clearEngineeringSession } from '@/api/device'
import { monitorApi, type LivePoint, type HistorySample } from '@/api/monitor'
import { errorMessage } from '@/api/http'
import type { DeviceNode, YcPoint, YxSignal, YkCommand, YtPoint, ControlLog } from '@/types/domain'
import type { EngineeringRecord, PointType } from '@/types/engineering'
export type MonitorScope = 'all' | 'alarm' | 'offline'
/** 监视页选中节点在会话内的持久化键，F5 刷新后恢复定位 */
const SELECTED_KEY = 'ems.monitor.selected-id'
/** 返回根到目标节点的路径；找不到返回 null */
function nodeTrail(nodes: DeviceNode[], id: string): DeviceNode[] | null {
  for (const n of nodes) {
    if (n.id === id) return [n]
    const nested = n.children ? nodeTrail(n.children, id) : null
    if (nested) return [n, ...nested]
  }
  return null
}
export const useMonitorStore = defineStore('monitor', () => {
  const auth = useAuthStore(),
    tree = ref<DeviceNode[]>([]),
    selectedId = ref(''),
    selected = ref<EngineeringRecord | null>(null),
    keyword = ref(''),
    scope = ref<MonitorScope>('all'),
    loading = ref(false),
    error = ref(''),
    paused = ref(false),
    lastUpdated = ref('—'),
    selectedYcCodes = ref<string[]>([]),
    curveWindow = ref('最近 30 分钟'),
    sampleInterval = ref('2 秒')
  const live = ref<Record<PointType, LivePoint[]>>({ yc: [], yx: [], yk: [], yt: [] }),
    totals = ref({ yc: 0, yx: 0, yk: 0, yt: 0 }),
    pages = ref({ yc: 1, yx: 1, yk: 1, yt: 1 }),
    history = ref<HistorySample[]>([]),
    historyError = ref(''),
    historyBusy = ref(false),
    historyTruncated = ref(false)
  const ytRows = ref<YtPoint[]>([]),
    controlLogRows = ref<ControlLog[]>([]),
    activeCommand = ref<YkCommand | null>(null),
    controlStep = ref<1 | 2 | 3>(1),
    controlReason = ref(''),
    controlRunning = ref(false),
    controlResult = ref<'idle' | 'success' | 'failed' | 'timeout'>('idle'),
    controlError = ref('')
  let epoch = 0,
    refreshing = false
  const display = (row: LivePoint) =>
    row.sample && row.quality === 'good' ? String(row.sample.value) : '—'
  const timestamp = (row: LivePoint) =>
    row.sample ? new Date(row.sample.timestamp).toLocaleTimeString('zh-CN', { hour12: false }) : '—'
  const ycList = computed<YcPoint[]>(() =>
    live.value.yc.map((r) => ({
      pointId: r.definition.point_id,
      code: r.definition.code,
      name: r.definition.name,
      value: display(r),
      unit: r.definition.unit,
      quality: r.quality === 'good' ? 'good' : r.quality === 'stale' ? 'timeout' : 'invalid',
      ts: timestamp(r),
      group: r.definition.semantic_code || '其他',
      series: history.value
        .filter((s) => s.point_id === r.definition.point_id)
        .map((s) => (s.quality_code === 0 && typeof s.value === 'number' ? s.value : NaN)),
    })),
  )
  const yxList = computed<YxSignal[]>(() =>
    live.value.yx.map((r) => ({
      name: r.definition.name,
      state: display(r),
      raw: (r.sample?.value ? '1' : '0') as '0' | '1',
      lastChange: timestamp(r),
      group: r.definition.semantic_code || '其他',
      level: r.quality === 'good' ? 'normal' : 'unknown',
    })),
  )
  const ykList = computed<YkCommand[]>(() =>
    auth.can('command.yk')
      ? live.value.yk.map((r) => ({
          pointId: r.definition.point_id,
          name: r.definition.name,
          desc: r.definition.description ? String(r.definition.description) : r.definition.code,
          current: display(r),
          action: String(r.definition.fixed_raw_value ?? '0'),
          disabledReason: '遥控执行服务暂未接通',
        }))
      : [],
  )
  const ytList = computed(() => (auth.can('command.yt') ? ytRows.value : []))
  const profile = computed(() => ({
    id: selectedId.value,
    name: selected.value?.name ?? '请选择设备',
    code: selected.value?.code ?? '—',
    type: selected.value?.device_type_code ?? '',
    vendor: selected.value?.vendor ?? '',
    firmware: selected.value?.firmware_version ?? '',
    icon: selected.value?.node_kind === 'component' ? 'battery' : 'cpu',
    dataSource: selected.value?.source_device_id || selected.value?.code || '—',
    status:
      live.value.yc.some((r) => r.quality === 'good') ||
      live.value.yx.some((r) => r.quality === 'good')
        ? 'online'
        : 'offline',
    metrics: ycList.value.slice(0, 6).map((r) => ({
      label: r.name,
      value: r.value,
      unit: r.unit,
      note: r.quality === 'good' ? `更新于 ${r.ts}` : '数据不可用',
      tone: r.quality === 'good' ? ('accent' as const) : ('warn' as const),
    })),
    tabCounts: {
      telemetry: totals.value.yc,
      signal: totals.value.yx,
      control: totals.value.yk,
      setpoint: totals.value.yt,
    },
  }))
  const selectedNode = computed(() => selected.value)
  /** 选中节点的祖先链，供树在刷新后展开定位 */
  const expandedKeys = computed(() => {
    const trail = selectedId.value ? nodeTrail(tree.value, selectedId.value) : null
    return trail ? trail.slice(0, -1).map((n) => n.id) : []
  })
  const filteredTree = computed(() => {
    const filter = (nodes: DeviceNode[]): DeviceNode[] =>
      nodes.flatMap((n) => {
        const children = filter(n.children ?? [])
        return !keyword.value || `${n.name} ${n.code}`.includes(keyword.value) || children.length
          ? [{ ...n, children }]
          : []
      })
    return filter(tree.value)
  })
  const counts = computed(() => {
    let all = 0
    const walk = (nodes: DeviceNode[]) =>
      nodes.forEach((n) => {
        if (n.isDevice) all++
        walk(n.children ?? [])
      })
    walk(tree.value)
    return { all, alarm: 0, offline: 0 }
  })
  const curveSeries = computed(() =>
    ycList.value.filter((r) => selectedYcCodes.value.includes(r.code)),
  )
  async function refresh() {
    if (refreshing || !selectedId.value || !auth.full) return
    const owner = epoch,
      id = selectedId.value
    refreshing = true
    error.value = ''
    try {
      const kinds = ['yc', 'yx', 'yk', 'yt'] as const
      const result = await Promise.allSettled(
        kinds.map(async (kind) => {
          try {
            return { ...(await monitorApi.points(id, kind, pages.value[kind])), error: '' }
          } catch (cause) {
            // 点位定义仍可供历史查询使用；实时服务失败不能生成或沿用旧样本。
            const configured = await deviceApi.points(id, kind, pages.value[kind])
            return {
              ...configured,
              items: configured.items.map(
                (definition): LivePoint => ({
                  definition,
                  sample: null,
                  quality: definition.enabled ? 'missing' : 'disabled',
                }),
              ),
              error: errorMessage(cause),
            }
          }
        }),
      )
      if (owner !== epoch || id !== selectedId.value) return
      const failures: string[] = []
      kinds.forEach((kind, i) => {
        const response = result[i]!
        if (response.status === 'fulfilled') {
          live.value[kind] = response.value.items
          totals.value[kind] = response.value.total
          if (response.value.error) failures.push(response.value.error)
        } else {
          live.value[kind] = []
          totals.value[kind] = 0
          failures.push(errorMessage(response.reason))
        }
      })
      error.value = [...new Set(failures)].join('；')
      const previous = new Map(ytRows.value.map((r) => [r.pointId, r]))
      ytRows.value = live.value.yt.map((r) => ({
        pointId: r.definition.point_id,
        name: r.definition.name,
        current: display(r),
        modified: previous.get(r.definition.point_id)?.modified ?? display(r),
        unit: r.definition.unit,
        min: Number(r.definition.range_min ?? -Number.MAX_VALUE),
        max: Number(r.definition.range_max ?? Number.MAX_VALUE),
      }))
      lastUpdated.value = failures.length
        ? '—'
        : new Date().toLocaleTimeString('zh-CN', { hour12: false })
    } catch (e) {
      if (owner === epoch) {
        error.value = errorMessage(e)
        live.value = { yc: [], yx: [], yk: [], yt: [] }
        ytRows.value = []
      }
    } finally {
      refreshing = false
    }
  }
  async function load() {
    const owner = epoch
    loading.value = true
    error.value = ''
    try {
      const nextTree = await deviceApi.tree()
      if (owner !== epoch) return
      tree.value = nextTree
      const first = (nodes: DeviceNode[]): string => {
        for (const n of nodes) {
          if (n.isDevice || n.kind === 'component') return n.id
          const nested = first(n.children ?? [])
          if (nested) return nested
        }
        return ''
      }
      if (!selectedId.value) {
        // F5 刷新后恢复上次选中的节点；节点已不存在时退回第一个可选节点
        const stored = sessionStorage.getItem(SELECTED_KEY) ?? ''
        selectedId.value = stored && nodeTrail(tree.value, stored) ? stored : first(tree.value)
      }
      if (selectedId.value) {
        const detail = await deviceApi.detail(selectedId.value)
        if (owner !== epoch) return
        selected.value = detail
        await refresh()
      }
    } catch (e) {
      if (owner === epoch) error.value = errorMessage(e)
    } finally {
      if (owner === epoch) loading.value = false
    }
  }
  async function selectDevice(id: string) {
    const owner = ++epoch
    selectedId.value = id
    sessionStorage.setItem(SELECTED_KEY, id)
    selected.value = null
    activeCommand.value = null
    history.value = []
    selectedYcCodes.value = []
    controlResult.value = 'idle'
    controlError.value = ''
    live.value = { yc: [], yx: [], yk: [], yt: [] }
    ytRows.value = []
    pages.value = { yc: 1, yx: 1, yk: 1, yt: 1 }
    try {
      const detail = await deviceApi.detail(id)
      if (owner !== epoch) return
      selected.value = detail
      await refresh()
    } catch (e) {
      if (owner === epoch) error.value = errorMessage(e)
    }
  }
  function toggleYc(code: string) {
    selectedYcCodes.value = selectedYcCodes.value.includes(code)
      ? selectedYcCodes.value.filter((c) => c !== code)
      : selectedYcCodes.value.length < 6
        ? [...selectedYcCodes.value, code]
        : selectedYcCodes.value
  }
  function openCommand(command: YkCommand) {
    if (!command.disabledReason && auth.can('command.yk')) {
      activeCommand.value = command
      controlStep.value = 1
      controlResult.value = 'idle'
      controlError.value = ''
    }
  }
  function closeCommand() {
    if (controlRunning.value) return
    activeCommand.value = null
    controlStep.value = 1
    controlResult.value = 'idle'
  }
  async function executeCommand() {
    if (!activeCommand.value || !controlReason.value.trim() || controlRunning.value) return
    controlRunning.value = true
    controlStep.value = 3
    controlError.value = ''
    const owner = epoch
    try {
      await monitorApi.submit({
        device_id: selectedId.value,
        point_id: activeCommand.value.pointId!,
        point_type: 'yk',
        value: Number(activeCommand.value.action),
        reason: controlReason.value.trim(),
        intent_id: stableId('intent'),
      })
      if (owner === epoch) controlError.value = '意图已受理，执行结果尚未确认'
    } catch (e) {
      if (owner === epoch) {
        controlError.value = errorMessage(e)
        controlResult.value = 'failed'
      }
    } finally {
      controlRunning.value = false
    }
  }
  async function submitSetpoints(reason: string) {
    if (!reason.trim()) throw new Error('请填写操作原因')
    const changed = ytRows.value.filter((r) => r.modified !== r.current)
    for (const row of changed) {
      const value = Number(row.modified)
      if (!Number.isFinite(value) || value < row.min || value > row.max)
        throw new Error('设定值超出允许范围')
      await monitorApi.submit({
        device_id: selectedId.value,
        point_id: row.pointId!,
        point_type: 'yt',
        value,
        reason,
        intent_id: stableId('intent'),
      })
    }
  }
  function setYtValue(name: string, value: string) {
    ytRows.value = ytRows.value.map((r) => (r.name === name ? { ...r, modified: value } : r))
  }
  function resetYtValue(name: string) {
    ytRows.value = ytRows.value.map((r) => (r.name === name ? { ...r, modified: r.current } : r))
  }
  async function queryHistory() {
    historyBusy.value = true
    historyError.value = ''
    const owner = epoch
    try {
      const ids = ycList.value
        .filter((r) => selectedYcCodes.value.includes(r.code))
        .map((r) => r.pointId!)
      if (!ids.length) {
        history.value = []
        return
      }
      const end = Date.now(),
        start = end - (curveWindow.value === '最近 2 小时' ? 7200000 : 1800000)
      const result = await monitorApi.history(selectedId.value, ids, start, end)
      if (owner === epoch) {
        history.value = result.items
        historyTruncated.value = result.truncated
      }
    } catch (e) {
      if (owner === epoch) {
        history.value = []
        historyError.value = errorMessage(e)
      }
    } finally {
      historyBusy.value = false
    }
  }
  function tick() {
    if (!paused.value) void refresh()
  }
  watch(
    () => auth.user?.user_id,
    () => {
      epoch++
      clearEngineeringSession()
      sessionStorage.removeItem(SELECTED_KEY)
      tree.value = []
      selectedId.value = ''
      selected.value = null
      live.value = { yc: [], yx: [], yk: [], yt: [] }
      history.value = []
      ytRows.value = []
      activeCommand.value = null
      controlLogRows.value = []
      error.value = ''
      clearHistory()
    },
  )
  function clearHistory() {
    historyError.value = ''
    historyTruncated.value = false
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
    controlError,
    profile,
    selectedNode,
    expandedKeys,
    ycList,
    yxList,
    ykList,
    ytList,
    filteredTree,
    counts,
    curveSeries,
    live,
    totals,
    pages,
    history,
    historyBusy,
    historyError,
    historyTruncated,
    selectDevice,
    toggleYc,
    openCommand,
    closeCommand,
    executeCommand,
    submitSetpoints,
    setYtValue,
    resetYtValue,
    tick,
    load,
    refresh,
    queryHistory,
  }
})
