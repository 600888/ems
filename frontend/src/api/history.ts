import { request } from './http'

export type HistoryPointType = 'yc' | 'yx'
export interface HistorySample {
  point_id: string
  /** 原始查询的采样时间，或按间隔查询的时间桶起点。 */
  timestamp: number
  /** 所选样本在时序库中的时间，旧服务可能省略。 */
  sample_timestamp?: number
  value: number | string | boolean | null
  quality_code: number
}
export interface HistoryQuery {
  device_id: string
  point_type: HistoryPointType
  point_ids: string[]
  start_ms: number
  end_ms: number
  interval_ms: number
  max_rows?: number
}
export interface HistoryResult {
  items: HistorySample[]
  truncated: boolean
}

export const historyApi = {
  query: (body: HistoryQuery, signal?: AbortSignal) =>
    request<HistoryResult>('/api/history/query', { max_rows: 10000, ...body }, { signal }),
}
