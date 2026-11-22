import { computed, ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { alarmApi } from '@/api/alarm'
import { deviceApi } from '@/api/device'
import { ApiError, errorMessage } from '@/api/http'
import { useAuthStore } from './auth'
import { scopedDeviceTree } from '@/domain/resource-scope'
import { ALARM_FRESH_MS, alarmFilterError, alarmRemarkError } from '@/domain/alarm'
import type { DeviceNode } from '@/types/domain'
import type { AlarmDetail, AlarmFilters, AlarmPage, AlarmRecord } from '@/types/alarm'

export const useAlarmStore = defineStore('alarm', () => {
  const auth = useAuthStore()
  const tree = ref<DeviceNode[]>([]),
    treeLoading = ref(false),
    treeError = ref('')
  const filters = ref<AlarmFilters>({
    view: 'active',
    device_root_id: '',
    severity: '',
    acknowledgement: '',
    recovery: '',
    keyword: '',
  })
  const page = ref(1),
    pageSize = ref(8),
    result = ref<AlarmPage | null>(null)
  const archivePending = ref(0)
  const loading = ref(false),
    error = ref(''),
    receivedAt = ref(0)
  const paused = ref(false),
    selectedIds = ref<string[]>([])
  const detail = ref<AlarmDetail | null>(null),
    detailId = ref(''),
    detailLoading = ref(false),
    detailError = ref(''),
    detailReceivedAt = ref(0)
  const ackBusy = ref(false),
    actionError = ref(''),
    notice = ref(''),
    noticeTone = ref<'ok' | 'warn' | 'info'>('info')
  const unresolvedIds = ref<string[]>([]),
    resolving = ref(false),
    exporting = ref(false)
  let sessionEpoch = 0,
    queryEpoch = 0,
    detailEpoch = 0,
    treeEpoch = 0,
    exportEpoch = 0
  let queryController: AbortController | undefined,
    detailController: AbortController | undefined,
    exportController: AbortController | undefined
  const selection = computed(
    () => result.value?.items.filter((row) => selectedIds.value.includes(row.alarm_id)) ?? [],
  )
  const pendingSelection = computed(() => selection.value.filter((row) => !row.acknowledgement))
  const allowedTree = computed(() =>
    scopedDeviceTree(tree.value, auth.session?.scope_mode, auth.session?.scopes ?? []),
  )
  const canRead = () => auth.can('alarm.read')

  function stopQueries() {
    queryEpoch++
    queryController?.abort()
    loading.value = false
  }
  function closeDetail() {
    if (ackBusy.value) return
    detailEpoch++
    detailController?.abort()
    detail.value = null
    detailId.value = ''
    detailError.value = ''
    detailReceivedAt.value = 0
    detailLoading.value = false
  }
  function deactivate() {
    sessionEpoch++
    ackBusy.value = false
    treeLoading.value = false
    stopQueries()
    closeDetail()
    exportEpoch++
    exportController?.abort()
    exporting.value = false
    selectedIds.value = []
  }
  async function loadTree() {
    if (!canRead()) return
    const owner = ++treeEpoch,
      session = sessionEpoch
    treeLoading.value = true
    treeError.value = ''
    try {
      const nodes = await deviceApi.tree()
      if (owner === treeEpoch && session === sessionEpoch) tree.value = nodes
    } catch (cause) {
      if (owner === treeEpoch && session === sessionEpoch) treeError.value = errorMessage(cause)
    } finally {
      if (owner === treeEpoch && session === sessionEpoch) treeLoading.value = false
    }
  }
  async function query() {
    if (!canRead()) return false
    const validation = alarmFilterError(filters.value)
    if (validation) {
      error.value = validation
      return false
    }
    queryController?.abort()
    const controller = new AbortController(),
      owner = ++queryEpoch,
      session = sessionEpoch
    queryController = controller
    loading.value = true
    try {
      const { data: next, meta } = await alarmApi.query(
        { ...filters.value },
        page.value,
        pageSize.value,
        controller.signal,
      )
      if (owner !== queryEpoch || session !== sessionEpoch) return false
      if (
        !Array.isArray(next.items) ||
        next.items.length > pageSize.value ||
        !next.summary ||
        !Number.isFinite(next.total)
      )
        throw new Error('告警查询响应异常，请刷新后重试')
      // 末页记录减少后自动退回有效页，不留空白的越界页。
      const last = Math.max(1, Math.ceil(next.total / pageSize.value))
      if (page.value > last) {
        page.value = last
        return await query()
      }
      result.value = next
      const archive = meta.alarm_archive
      archivePending.value =
        archive &&
        typeof archive === 'object' &&
        'pending' in archive &&
        typeof archive.pending === 'number' &&
        archive.pending > 0
          ? archive.pending
          : 0
      receivedAt.value = Date.now()
      error.value = ''
      selectedIds.value = selectedIds.value.filter((id) =>
        next.items.some((row) => row.alarm_id === id),
      )
      return true
    } catch (cause) {
      if (owner === queryEpoch && session === sessionEpoch) error.value = errorMessage(cause)
      return false
    } finally {
      if (owner === queryEpoch && session === sessionEpoch) loading.value = false
    }
  }
  async function applyFilters(next: AlarmFilters) {
    if (ackBusy.value) return
    stopQueries()
    closeDetail()
    filters.value = { ...next, keyword: next.keyword.trim() }
    page.value = 1
    selectedIds.value = []
    result.value = null
    receivedAt.value = 0
    error.value = ''
    notice.value = ''
    return query()
  }
  async function paginate(next: number, size = pageSize.value) {
    if (ackBusy.value) return
    stopQueries()
    page.value = size === pageSize.value ? next : 1
    pageSize.value = size
    result.value = null
    receivedAt.value = 0
    selectedIds.value = []
    return query()
  }
  async function openDetail(id: string) {
    if (!id || !canRead() || ackBusy.value) return
    detailController?.abort()
    const controller = new AbortController(),
      owner = ++detailEpoch,
      session = sessionEpoch
    detailController = controller
    detailId.value = id
    detail.value = null
    detailError.value = ''
    detailReceivedAt.value = 0
    detailLoading.value = true
    try {
      const value = await alarmApi.detail(id, controller.signal)
      if (owner !== detailEpoch || session !== sessionEpoch) return
      if (value.alarm_id !== id || !Array.isArray(value.events)) throw new Error('告警详情响应异常')
      detail.value = value
      detailReceivedAt.value = Date.now()
      if (value.acknowledgement && unresolvedIds.value.includes(id)) {
        unresolvedIds.value = unresolvedIds.value.filter((pending) => pending !== id)
        if (!unresolvedIds.value.length) {
          actionError.value = ''
          notice.value = '已读取服务端记录，告警确认结果已核实。'
          noticeTone.value = 'ok'
        }
      }
    } catch (cause) {
      if (owner === detailEpoch && session === sessionEpoch) detailError.value = errorMessage(cause)
    } finally {
      if (owner === detailEpoch && session === sessionEpoch) detailLoading.value = false
    }
  }
  function confirmationBlock(ids: string[], now = Date.now()) {
    if (!auth.can('alarm.ack')) return '当前账号没有告警确认权限'
    if (ackBusy.value || resolving.value) return '正在处理，请稍候'
    if (loading.value || detailLoading.value) return '正在刷新告警状态，请稍候'
    if (unresolvedIds.value.length) return '上次提交结果未确认，请先刷新确认结果'
    const timestamp =
      ids.length === 1 && detail.value?.alarm_id === ids[0]
        ? detailReceivedAt.value
        : receivedAt.value
    if (!timestamp || now - timestamp > ALARM_FRESH_MS || error.value)
      return '告警状态已过期或连接中断，请先刷新数据'
    if (!ids.length) return '请先选择要确认的告警'
    return ''
  }
  async function acknowledge(ids: string[], remark: string) {
    const blocked = confirmationBlock(ids) || alarmRemarkError(remark)
    if (blocked) {
      actionError.value = blocked
      return false
    }
    const unique = [...new Set(ids)],
      session = sessionEpoch
    if (unique.length > 200) {
      actionError.value = '单次最多确认 200 条告警'
      return false
    }
    ackBusy.value = true
    actionError.value = ''
    notice.value = ''
    let succeeded = false
    try {
      const reply = await alarmApi.acknowledge(unique, remark.trim())
      if (session !== sessionEpoch) return false
      if (
        !Array.isArray(reply.items) ||
        unique.some((id) => reply.items.filter((item) => item.alarm_id === id).length !== 1) ||
        reply.items.some(
          (item) =>
            !unique.includes(item.alarm_id) ||
            !['acknowledged', 'already_acknowledged', 'failed'].includes(item.status),
        )
      )
        throw new ApiError(502, 'invalid_response', '确认结果不完整', 'Unknown')
      const done = reply.items.filter((item) => item.status === 'acknowledged').length
      const skipped = reply.items.filter((item) => item.status === 'already_acknowledged').length
      const failed = reply.items.filter((item) => item.status === 'failed')
      notice.value = `确认成功 ${done} 条，已确认跳过 ${skipped} 条，失败 ${failed.length} 条。`
      noticeTone.value = failed.length ? 'warn' : 'ok'
      actionError.value = failed
        .map((item) => `${item.alarm_id}：${item.message || '确认失败'}`)
        .join('；')
      selectedIds.value = failed.map((item) => item.alarm_id)
      succeeded = !failed.length
    } catch (cause) {
      if (session !== sessionEpoch) return false
      const unknown = !(cause instanceof ApiError) || cause.effect === 'Unknown'
      if (unknown) unresolvedIds.value = unique
      actionError.value =
        errorMessage(cause) + (unknown ? '。提交结果未确认，请先刷新确认结果，勿重复提交。' : '')
    } finally {
      if (session === sessionEpoch) ackBusy.value = false
    }
    if (session === sessionEpoch) {
      await query()
      if (detailId.value) await openDetail(detailId.value)
    }
    return succeeded
  }
  async function resolveAcknowledgements() {
    if (!canRead() || resolving.value || ackBusy.value) return
    const session = sessionEpoch,
      ids = [...unresolvedIds.value]
    resolving.value = true
    try {
      const rows = await Promise.all(ids.map((id) => alarmApi.detail(id)))
      if (session !== sessionEpoch) return
      if (rows.some((row, i) => row.alarm_id !== ids[i])) throw new Error('确认结果响应异常')
      const pending = rows.filter((row) => !row.acknowledgement)
      unresolvedIds.value = []
      selectedIds.value = pending.map((row) => row.alarm_id)
      actionError.value = ''
      notice.value = `已刷新：${rows.length - pending.length} 条已确认，${pending.length} 条仍未确认，可检查后重试。`
      noticeTone.value = 'info'
      await query()
      if (detailId.value) await openDetail(detailId.value)
    } catch (cause) {
      if (session === sessionEpoch) actionError.value = errorMessage(cause)
    } finally {
      if (session === sessionEpoch) resolving.value = false
    }
  }
  async function exportRows(): Promise<AlarmRecord[] | null> {
    if (!canRead() || exporting.value || !result.value || error.value) return null
    const session = sessionEpoch,
      owner = ++exportEpoch,
      controller = new AbortController()
    exportController = controller
    exporting.value = true
    try {
      const value = await alarmApi.export({ ...filters.value }, controller.signal)
      if (session !== sessionEpoch || owner !== exportEpoch) return null
      if (!Array.isArray(value.items) || value.items.length > 10000)
        throw new Error('导出结果超出限制')
      notice.value = value.truncated
        ? '导出已达 10000 条上限，请缩小查询范围。'
        : `已导出 ${value.items.length} 条告警记录。`
      noticeTone.value = value.truncated ? 'warn' : 'ok'
      return value.items
    } catch (cause) {
      if (session === sessionEpoch && owner === exportEpoch) {
        notice.value = errorMessage(cause)
        noticeTone.value = 'warn'
      }
      return null
    } finally {
      if (session === sessionEpoch && owner === exportEpoch) exporting.value = false
    }
  }
  // 轮询会话只更新时间戳时不清空；身份、权限或资源范围变化立即隔离所有异步结果。
  watch(
    () =>
      JSON.stringify([
        auth.user?.user_id,
        auth.session?.kind,
        auth.session?.permissions,
        auth.session?.scope_mode,
        auth.session?.scopes,
      ]),
    () => {
      sessionEpoch++
      ackBusy.value = false
      deactivate()
      treeEpoch++
      tree.value = []
      treeLoading.value = false
      treeError.value = ''
      result.value = null
      receivedAt.value = 0
      filters.value = {
        view: 'active',
        device_root_id: '',
        severity: '',
        acknowledgement: '',
        recovery: '',
        keyword: '',
      }
      page.value = 1
      paused.value = false
      error.value = ''
      unresolvedIds.value = []
      resolving.value = false
      actionError.value = ''
      notice.value = ''
    },
    { flush: 'sync' },
  )

  return {
    tree,
    allowedTree,
    treeLoading,
    treeError,
    filters,
    page,
    pageSize,
    result,
    archivePending,
    loading,
    error,
    receivedAt,
    paused,
    selectedIds,
    selection,
    pendingSelection,
    detail,
    detailId,
    detailLoading,
    detailError,
    detailReceivedAt,
    ackBusy,
    actionError,
    notice,
    noticeTone,
    unresolvedIds,
    resolving,
    exporting,
    loadTree,
    query,
    applyFilters,
    paginate,
    openDetail,
    closeDetail,
    confirmationBlock,
    acknowledge,
    resolveAcknowledgements,
    exportRows,
    deactivate,
  }
})
