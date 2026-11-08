import type { HistorySample } from '@/api/history'

export const HISTORY_INTERVAL_MS: Record<string, number> = {
  原始数据: 0,
  '1 分钟': 60000,
  '5 分钟': 300000,
  '15 分钟': 900000,
  '1 小时': 3600000,
}
export const HISTORY_MAX_RANGE_MS = 30 * 86400000

/** 无效质量保留为断点，遥信布尔值映射到 0 / 1。 */
export function chartValue(sample: HistorySample): number | null {
  if (sample.quality_code !== 0) return null
  if (typeof sample.value === 'boolean') return Number(sample.value)
  return typeof sample.value === 'number' && Number.isFinite(sample.value) ? sample.value : null
}

/** 包含完整日期及毫秒，避免原始样本在明细和导出中丢失时间精度。 */
export function sampleTime(timestamp: number): string {
  const d = new Date(timestamp)
  const pad = (value: number, width = 2) => String(value).padStart(width, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`
}

/** CSV 文本完整转义，并防止设备/测点名称被表格软件解释为公式。 */
export function csvCell(value: string): string {
  const numeric = /^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(value)
  const safe = /^[\s]*[=+@-]/.test(value) && !numeric ? `'${value}` : value
  return `"${safe.replaceAll('"', '""')}"`
}
