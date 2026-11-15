import type { AlarmFilters, AlarmRecord, AlarmSeverity } from '@/types/alarm'
import { csvCell } from '@/domain/history'

export const alarmLevels = {
  critical: { label: '严重', tone: 'danger', icon: 'octagon-alert' },
  warning: { label: '警告', tone: 'warn', icon: 'triangle-alert' },
  info: { label: '提示', tone: 'accent', icon: 'info' },
} as const satisfies Record<AlarmSeverity, { label: string; tone: string; icon: string }>
export const ALARM_MAX_RANGE_MS = 31 * 86400000
export const ALARM_POLL_MS = 5000
export const ALARM_FRESH_MS = 15000

export function alarmTime(value: number | null | undefined, short = false) {
  if (value == null || !Number.isFinite(value)) return '—'
  return new Intl.DateTimeFormat('sv-SE', {
    timeZone: 'Asia/Shanghai',
    ...(short ? {} : { year: 'numeric', month: '2-digit', day: '2-digit' }),
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hourCycle: 'h23',
  }).format(value)
}

export function alarmDuration(row: AlarmRecord, now: number) {
  const seconds = Math.max(0, Math.floor(((row.cleared_at_ms ?? now) - row.occurred_at_ms) / 1000))
  if (seconds < 60) return `${seconds} 秒`
  if (seconds < 3600) return `${Math.floor(seconds / 60)} 分 ${seconds % 60} 秒`
  if (seconds < 86400)
    return `${Math.floor(seconds / 3600)} 时 ${Math.floor((seconds % 3600) / 60)} 分`
  return `${Math.floor(seconds / 86400)} 天 ${Math.floor((seconds % 86400) / 3600)} 时`
}

export function alarmRange(days: 1 | 7 | 30, now = Date.now()): [number, number] {
  // “今天”始终以站端北京时间零点为起点，不受浏览器所在时区影响。
  const midnight = Math.floor((now + 8 * 3600000) / 86400000) * 86400000 - 8 * 3600000
  return [days === 1 ? midnight : now - days * 86400000, now]
}

export function alarmFilterError(filters: AlarmFilters) {
  if (filters.view !== 'history') return ''
  const { start_ms: start, end_ms: end } = filters
  if (start == null || end == null || !Number.isFinite(start) || !Number.isFinite(end))
    return '请选择完整的发生时间范围'
  if (start > end) return '开始时间不得晚于结束时间'
  if (end - start > ALARM_MAX_RANGE_MS) return '单次查询时间范围不可超过 31 天'
  return ''
}

export function alarmRemarkError(value: string) {
  if (!value.trim()) return '请填写确认原因或现场处置情况'
  if (Array.from(value.trim()).length > 500) return '确认备注不可超过 500 个字符'
  return ''
}

export function alarmsCsv(rows: AlarmRecord[]) {
  return (
    '\ufeff' +
    [
      [
        '告警编号',
        '级别',
        '告警名称',
        '告警编码',
        '设备',
        '设备路径',
        '发生时间（UTC+8）',
        '恢复时间（UTC+8）',
        '确认状态',
        '确认人',
        '确认时间（UTC+8）',
        '处置备注',
      ],
      ...rows.map((row) => [
        row.alarm_id,
        alarmLevels[row.severity].label,
        row.name,
        row.alarm_code,
        row.device_name,
        row.device_path,
        alarmTime(row.occurred_at_ms),
        alarmTime(row.cleared_at_ms),
        row.acknowledgement ? '已确认' : '未确认',
        row.acknowledgement?.display_name ?? '',
        alarmTime(row.acknowledgement?.at_ms),
        row.acknowledgement?.remark ?? '',
      ]),
    ]
      .map((row) => row.map(csvCell).join(','))
      .join('\r\n')
  )
}
