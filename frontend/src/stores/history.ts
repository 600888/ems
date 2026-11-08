import { computed, ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { useAuthStore } from './auth'
import { deviceApi } from '@/api/device'
import { historyApi, type HistorySample } from '@/api/history'
import {
  HISTORY_INTERVAL_MS,
  HISTORY_MAX_RANGE_MS,
  chartValue,
  csvCell,
  sampleTime,
} from '@/domain/history'
import { errorMessage } from '@/api/http'
import type { ChartSeries } from '@/types/ui'
import type { DeviceNode } from '@/types/domain'
import type { Point } from '@/types/engineering'

/** 历史查询状态：配置目录、查询生命周期与结果视图。 */

export type HistoryKindFilter = 'all' | 'yc' | 'yx'
export type QuickRangeKey = '1h' | '24h' | '7d' | '30d'

/** 测点子节点（设备树勾选用） */
export interface HistoryPointNode {
  id: string
  name: string
  isPoint: true
  icon: string
  pointKind: 'yc' | 'yx'
  pointId: string
  code: string
  unit: string
  deviceId: string
  deviceName: string
}

/** 设备节点：children 在展开时懒加载为测点子节点 */
export interface HistoryDeviceNode extends Omit<DeviceNode, 'children'> {
  children?: HistoryTreeChild[]
  /** 测点子节点是否已加载 */
  pointsLoaded?: boolean
  isPoint?: false
}

export type HistoryTreeChild = HistoryDeviceNode | HistoryPointNode

/** 已勾选测点 */
export interface SelectedPoint {
  deviceId: string
  deviceName: string
  pointId: string
  code: string
  name: string
  unit: string
  kind: 'yc' | 'yx'
}

export const HISTORY_LIMIT = 6
/** 曲线配色，与设备监视 · 曲线分析画板一致 */
export const HISTORY_COLORS = ['#55d9ef', '#287bff', '#48d8ad', '#f5bc62', '#ff7883', '#a6a0ff']
export const HISTORY_INTERVALS = Object.keys(HISTORY_INTERVAL_MS)
const QUICK_SPANS: Record<QuickRangeKey, number> = {
  '1h': 3600_000,
  '24h': 86_400_000,
  '7d': 7 * 86_400_000,
  '30d': 30 * 86_400_000,
}

const pad = (n: number) => String(n).padStart(2, '0')
/** 与画板一致的时间文案：MM-dd HH:mm */
function fmtTime(ts: number, withDate = true) {
  const d = new Date(ts)
  const hm = `${pad(d.getHours())}:${pad(d.getMinutes())}`
  return withDate ? `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${hm}` : hm
}

export const useHistoryStore = defineStore('history', () => {
  const auth = useAuthStore()
  const tree = ref<HistoryDeviceNode[]>([])
  const keyword = ref('')
  const loading = ref(false)
  const error = ref('')
  /** 操作提示（勾选上限等），用 InfoBar 展示 */
  const notice = ref('')

  const selected = ref<SelectedPoint[]>([])
  const kindFilter = ref<HistoryKindFilter>('all')
  const quickRange = ref<QuickRangeKey>('24h')
  /** 自定义时间范围（ms），设置后快捷范围取消高亮 */
  const customRange = ref<[number, number] | null>(null)
  const interval = ref('5 分钟')

  const busy = ref(false)
  const queryError = ref('')
  const truncated = ref(false)
  const lastQueryAt = ref('—')
  const seriesData = ref<{ pointId: string; samples: HistorySample[] }[]>([])
  const page = ref(1)
  const pageSize = 20

  const pointsCache = new Map<string, HistoryPointNode[]>()
  const pendingPoints = new Map<string, Promise<HistoryPointNode[]>>()
  let sessionEpoch = 0
  let queryOwner: symbol | null = null
  let queryController: AbortController | null = null
  let loadOwner: symbol | null = null
  const rangeAnchor = ref(Date.now())

  const range = computed<[number, number]>(() => {
    if (customRange.value) return customRange.value
    const end = rangeAnchor.value
    return [end - QUICK_SPANS[quickRange.value], end]
  })

  const rangeText = computed(() => {
    const [start, end] = range.value
    return `${fmtTime(start)} — ${fmtTime(end)}`
  })

  const filteredTree = computed<HistoryDeviceNode[]>(() => {
    if (!keyword.value.trim()) return tree.value
    const search = keyword.value.trim().toLocaleLowerCase()
    const filter = (nodes: HistoryTreeChild[]): HistoryTreeChild[] =>
      nodes.flatMap((n) => {
        const children = filter((n as HistoryDeviceNode).children ?? [])
        const code = n.code ?? ''
        const self = `${n.name} ${code}`.toLocaleLowerCase().includes(search)
        return self ? [n] : children.length ? [{ ...n, children }] : []
      })
    return filter(tree.value) as HistoryDeviceNode[]
  })

  /** 按数据类型过滤后的已选测点（图表与明细的列来源） */
  const visiblePoints = computed(() =>
    selected.value.filter((p) => kindFilter.value === 'all' || p.kind === kindFilter.value),
  )

  const isSelected = (pointId: string) => selected.value.some((p) => p.pointId === pointId)

  /** 设备勾选态：all 全选（square-check）/ 其余（square） */
  function deviceCheckState(node: HistoryDeviceNode): 'all' | 'some' | 'none' {
    const pts = (node.children ?? []).filter((c): c is HistoryPointNode => c.isPoint === true)
    if (!pts.length || !node.pointsLoaded) return 'none'
    const hits = pts.filter((p) => isSelected(p.pointId)).length
    return hits === pts.length ? 'all' : hits ? 'some' : 'none'
  }

  function pointNodeOf(
    definition: Point,
    kind: 'yc' | 'yx',
    device: HistoryDeviceNode,
  ): HistoryPointNode {
    return {
      id: `point:${definition.point_id}`,
      name: definition.name,
      isPoint: true,
      icon: kind === 'yc' ? 'gauge' : 'radio',
      pointKind: kind,
      pointId: definition.point_id,
      code: definition.code,
      unit: definition.unit ?? '',
      deviceId: device.id,
      deviceName: device.name,
    }
  }

  function findDevice(
    id: string,
    nodes: HistoryTreeChild[] = tree.value,
  ): HistoryDeviceNode | undefined {
    for (const node of nodes) {
      if (node.isPoint) continue
      if (node.id === id) return node
      const found = findDevice(id, node.children ?? [])
      if (found) return found
    }
  }

  /** 一次加载完整点位分页；任意分类失败时不缓存残缺目录，允许重试。 */
  async function ensurePoints(input: HistoryDeviceNode): Promise<HistoryPointNode[]> {
    const node = findDevice(input.id) ?? input
    if (node.kind !== 'device' && node.kind !== 'component') return []
    if (node.pointsLoaded)
      return (node.children ?? []).filter(
        (child): child is HistoryPointNode => child.isPoint === true,
      )
    const attach = (items: HistoryPointNode[]) => {
      node.children = [...(node.children ?? []).filter((child) => !child.isPoint), ...items]
      node.pointsLoaded = true
      return items
    }
    if (pointsCache.has(node.id)) return attach(pointsCache.get(node.id)!)
    const pending = pendingPoints.get(node.id)
    if (pending) return pending
    const epoch = sessionEpoch
    const loadKind = async (kind: 'yc' | 'yx') => {
      const items: HistoryPointNode[] = []
      for (let page = 1; page <= 500; page++) {
        const result = await deviceApi.points(node.id, kind, page)
        if (epoch !== sessionEpoch) return []
        items.push(...result.items.map((point) => pointNodeOf(point, kind, node)))
        if (page * 200 >= result.total) return items
        if (!result.items.length) throw new Error('测点分页已变化，请重新加载设备目录。')
      }
      throw new Error('测点数量超出目录加载限制。')
    }
    const promise = (async () => {
      try {
        const groups = await Promise.all([loadKind('yc'), loadKind('yx')])
        if (epoch !== sessionEpoch) return []
        const items = groups.flat()
        pointsCache.set(node.id, items)
        error.value = ''
        return attach(items)
      } catch (error) {
        if (epoch !== sessionEpoch) return []
        throw error
      } finally {
        if (epoch === sessionEpoch) pendingPoints.delete(node.id)
      }
    })()
    pendingPoints.set(node.id, promise)
    return promise
  }

  function addPoints(pts: HistoryPointNode[]) {
    const addable = pts
      .filter((p) => !isSelected(p.pointId))
      .slice(0, Math.max(0, HISTORY_LIMIT - selected.value.length))
    if (addable.length < pts.filter((p) => !isSelected(p.pointId)).length)
      notice.value = `最多同时对比 ${HISTORY_LIMIT} 个测点，超出部分未加入。`
    for (const p of addable) {
      selected.value.push({
        deviceId: p.deviceId,
        deviceName: p.deviceName,
        pointId: p.pointId,
        code: p.code,
        name: p.name,
        unit: p.unit,
        kind: p.pointKind,
      })
    }
  }

  /** 勾选设备：已全选则整设备取消，否则尽量加入其全部测点 */
  async function toggleDevice(node: HistoryDeviceNode) {
    notice.value = ''
    const epoch = sessionEpoch
    let pts: HistoryPointNode[]
    try {
      pts = await ensurePoints(node)
    } catch (e) {
      error.value = errorMessage(e)
      return
    }
    if (epoch !== sessionEpoch) return
    if (!pts.length) {
      notice.value = `${node.name} 下没有遥测 / 遥信点位。`
      return
    }
    const ids = pts.map((p) => p.pointId)
    if (ids.every((id) => isSelected(id))) {
      selected.value = selected.value.filter((p) => !ids.includes(p.pointId))
      return
    }
    addPoints(pts)
  }

  function togglePoint(point: HistoryPointNode) {
    notice.value = ''
    if (isSelected(point.pointId)) {
      selected.value = selected.value.filter((p) => p.pointId !== point.pointId)
      return
    }
    addPoints([point])
  }

  function removePoint(pointId: string) {
    selected.value = selected.value.filter((p) => p.pointId !== pointId)
  }

  function clearSelection() {
    selected.value = []
    notice.value = ''
  }

  function resetFilters() {
    quickRange.value = '24h'
    customRange.value = null
    interval.value = '5 分钟'
    kindFilter.value = 'all'
    notice.value = ''
  }

  async function load() {
    const owner = Symbol('history-load')
    loadOwner = owner
    loading.value = true
    error.value = ''
    try {
      const nextTree = await deviceApi.tree()
      if (loadOwner !== owner) return
      const convert = (n: DeviceNode): HistoryDeviceNode => ({
        ...n,
        children: n.children?.map(convert),
      })
      sessionEpoch++
      pointsCache.clear()
      pendingPoints.clear()
      selected.value = []
      invalidateQuery()
      tree.value = nextTree.map(convert)
    } catch (e) {
      if (loadOwner === owner) error.value = errorMessage(e)
    } finally {
      if (loadOwner === owner) loading.value = false
    }
  }
  function invalidateQuery() {
    queryOwner = null
    queryController?.abort()
    queryController = null
    busy.value = false
    seriesData.value = []
    queryError.value = ''
    truncated.value = false
    lastQueryAt.value = '—'
    page.value = 1
    rangeAnchor.value = Date.now()
  }

  async function query() {
    invalidateQuery()
    notice.value = ''
    const pts = [...visiblePoints.value]
    if (!pts.length) {
      notice.value = '请先勾选要查询的测点。'
      return
    }
    rangeAnchor.value = Date.now()
    const [start, end] = range.value
    if (
      !Number.isSafeInteger(start) ||
      !Number.isSafeInteger(end) ||
      start < 0 ||
      end <= start ||
      end - start > HISTORY_MAX_RANGE_MS ||
      HISTORY_INTERVAL_MS[interval.value] === undefined
    ) {
      queryError.value = '请选择有效时间范围（开始时间早于结束时间，跨度不超过 30 天）和采样间隔。'
      return
    }
    const owner = Symbol('history-query')
    queryOwner = owner
    const controller = new AbortController()
    queryController = controller
    busy.value = true
    const groups = new Map<string, SelectedPoint[]>()
    for (const point of pts) {
      const key = `${point.deviceId}:${point.kind}`
      groups.set(key, [...(groups.get(key) ?? []), point])
    }
    try {
      const results = await Promise.allSettled(
        [...groups.values()].map((points) =>
          historyApi.query(
            {
              device_id: points[0]!.deviceId,
              point_type: points[0]!.kind,
              point_ids: points.map((p) => p.pointId),
              start_ms: start,
              end_ms: end,
              interval_ms: HISTORY_INTERVAL_MS[interval.value]!,
            },
            controller.signal,
          ),
        ),
      )
      if (queryOwner !== owner) return
      const merged = new Map<string, HistorySample[]>()
      const failures: string[] = []
      let successes = 0
      results.forEach((result, index) => {
        if (result.status === 'rejected') {
          const group = [...groups.values()][index]!
          failures.push(
            `${group[0]!.deviceName} · ${group[0]!.kind === 'yc' ? '遥测' : '遥信'}：${errorMessage(result.reason)}`,
          )
          return
        }
        successes++
        truncated.value ||= result.value.truncated
        for (const sample of result.value.items) {
          const list = merged.get(sample.point_id) ?? []
          list.push(sample)
          merged.set(sample.point_id, list)
        }
      })
      seriesData.value = pts.map((p) => ({
        pointId: p.pointId,
        samples: (merged.get(p.pointId) ?? []).sort((a, b) => a.timestamp - b.timestamp),
      }))
      queryError.value = failures.length ? `部分查询未完成：${failures.join('；')}` : ''
      if (successes) lastQueryAt.value = new Date(end).toLocaleString('zh-CN', { hour12: false })
    } finally {
      if (queryOwner === owner) {
        busy.value = false
        queryController = null
      }
    }
  }

  /** 服务端采样结果直接绘图，无效样本断开，布尔遥信映射到 0 / 1。 */
  const chartSeries = computed<ChartSeries[]>(() =>
    visiblePoints.value.map((p) => {
      const samples = seriesData.value.find((s) => s.pointId === p.pointId)?.samples ?? []
      return {
        name: `${p.deviceName} · ${p.name}${p.unit ? ` (${p.unit})` : ''}`,
        color:
          HISTORY_COLORS[
            selected.value.findIndex((point) => point.pointId === p.pointId) % HISTORY_COLORS.length
          ]!,
        timestamps: samples.map((s) => s.timestamp),
        values: samples.map(chartValue),
      }
    }),
  )

  const hasChartData = computed(() =>
    chartSeries.value.some((s) => s.values.some((v) => v !== null && Number.isFinite(v))),
  )

  /** 纵轴刻度（自上而下），含 10% 余量 */
  const yTicks = computed(() => {
    const values = chartSeries.value
      .flatMap((s) => s.values)
      .filter((v): v is number => typeof v === 'number')
    if (!values.length) return [1, 0.75, 0.5, 0.25, 0]
    const lo = Math.min(...values),
      hi = Math.max(...values),
      span = hi - lo || Math.abs(hi) || 1
    const top = hi + span * 0.1,
      bottom = lo - span * 0.1
    return [0, 1, 2, 3, 4].map((i) => bottom + ((top - bottom) * (4 - i)) / 4)
  })

  const xLabels = computed(() => {
    const times = chartSeries.value.flatMap((s) => s.timestamps ?? [])
    if (times.length < 2) return []
    const lo = Math.min(...times),
      hi = Math.max(...times),
      span = hi - lo
    return Array.from({ length: 6 }, (_, i) => {
      const t = lo + (span * i) / 5
      return fmtTime(t, span > 6 * 3600_000)
    })
  })

  /** 明细行：按时间戳对齐各测点 */
  const tableRows = computed(() => {
    type Cell = { text: string; good: boolean }
    const rows = new Map<number, Cell[]>()
    const points = visiblePoints.value
    for (const p of points) {
      const samples = seriesData.value.find((s) => s.pointId === p.pointId)?.samples ?? []
      for (const s of samples) {
        const cells: Cell[] =
          rows.get(s.timestamp) ??
          Array.from({ length: points.length }, () => ({ text: '—', good: false }))
        const good =
          s.quality_code === 0 &&
          s.value !== null &&
          (typeof s.value !== 'number' || Number.isFinite(s.value))
        const raw = typeof s.value === 'boolean' ? Number(s.value) : s.value
        const text = good ? String(raw) : '—'
        cells[points.indexOf(p)] = { text, good }
        rows.set(s.timestamp, cells)
      }
    }
    return [...rows.entries()]
      .sort((a, b) => a[0] - b[0])
      .map(([ts, cells]) => ({
        timestamp: ts,
        time: sampleTime(ts),
        cells: cells.map((c) => c ?? { text: '—', good: false }),
        allGood: cells.every((c) => c?.good),
      }))
  })

  const pagedRows = computed(() =>
    tableRows.value.slice((page.value - 1) * pageSize, page.value * pageSize),
  )

  function exportCsv() {
    const points = visiblePoints.value
    if (!points.length || !tableRows.value.length) return
    const head = [
      '时间',
      ...points.map((p) => `${p.deviceName} · ${p.name}${p.unit ? `(${p.unit})` : ''}`),
      '质量',
    ]
    const lines = [
      head.map(csvCell).join(','),
      ...tableRows.value.map((row) =>
        [row.time, ...row.cells.map((c) => c.text), row.allGood ? '有效' : '部分无效']
          .map(csvCell)
          .join(','),
      ),
    ]
    const url = URL.createObjectURL(
      new Blob(['\ufeff' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' }),
    )
    const a = document.createElement('a')
    a.href = url
    a.download = '历史数据.csv'
    a.click()
    window.setTimeout(() => URL.revokeObjectURL(url), 0)
  }

  watch([selected, kindFilter, quickRange, customRange, interval], invalidateQuery, {
    deep: true,
    flush: 'sync',
  })

  watch(
    () => auth.session,
    () => {
      sessionEpoch++
      loadOwner = null
      loading.value = false
      tree.value = []
      selected.value = []
      pointsCache.clear()
      pendingPoints.clear()
      invalidateQuery()
      error.value = ''
      notice.value = ''
      keyword.value = ''
    },
    { flush: 'sync' },
  )

  return {
    tree,
    keyword,
    loading,
    error,
    notice,
    selected,
    kindFilter,
    quickRange,
    customRange,
    interval,
    busy,
    queryError,
    truncated,
    lastQueryAt,
    page,
    pageSize,
    range,
    rangeText,
    filteredTree,
    visiblePoints,
    isSelected,
    deviceCheckState,
    ensurePoints,
    toggleDevice,
    togglePoint,
    removePoint,
    clearSelection,
    resetFilters,
    load,
    query,
    chartSeries,
    hasChartData,
    yTicks,
    xLabels,
    tableRows,
    pagedRows,
    exportCsv,
  }
})
