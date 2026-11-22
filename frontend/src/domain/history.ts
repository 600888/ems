import type { HistorySample } from '@/api/history'

export const HISTORY_INTERVAL_MS: Record<string, number> = {
  '5s': 5000,
  '30s': 30000,
  '1分钟': 60000,
  '5分钟': 300000,
  '30分钟': 1800000,
}
export const HISTORY_MAX_RANGE_MS = 30 * 86400000

/** 无效质量保留为断点，遥信布尔值映射到 0 / 1。 */
export function chartValue(sample: HistorySample): number | null {
  if (sample.quality_code !== 0) return null
  if (typeof sample.value === 'boolean') return Number(sample.value)
  return typeof sample.value === 'number' && Number.isFinite(sample.value) ? sample.value : null
}

/** 使用归档样本的时间绘图，在跳过的时间桶处断开曲线。 */
export function historyChartSamples(samples: HistorySample[], intervalMs: number) {
  const timestamps: number[] = []
  const values: (number | null)[] = []
  let previousBucket: number | undefined
  for (const sample of samples) {
    if (
      intervalMs > 0 &&
      previousBucket !== undefined &&
      sample.timestamp - previousBucket > intervalMs
    ) {
      timestamps.push(previousBucket + intervalMs)
      values.push(null)
    }
    timestamps.push(sample.sample_timestamp ?? sample.timestamp)
    values.push(chartValue(sample))
    previousBucket = sample.timestamp
  }
  return { timestamps, values }
}

/** 包含完整日期及毫秒，避免原始样本在明细和导出中丢失时间精度。 */
export function sampleTime(timestamp: number): string {
  const d = new Date(timestamp)
  const pad = (value: number, width = 2) => String(value).padStart(width, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`
}

/** 按图表当前时间跨度选择横轴刻度的精度。 */
export function chartAxisTime(timestamp: number, span: number): string {
  const time = sampleTime(timestamp)
  if (span > 6 * 3600_000) return time.slice(5, 16)
  if (span >= 5 * 60_000) return time.slice(11, 16)
  if (span >= 5000) return time.slice(11, 19)
  return time.slice(11)
}

/** CSV 文本完整转义，并防止设备/测点名称被表格软件解释为公式。 */
export function csvCell(value: string): string {
  const numeric = /^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(value)
  const safe = /^[\s]*[=+@-]/.test(value) && !numeric ? `'${value}` : value
  return `"${safe.replaceAll('"', '""')}"`
}
