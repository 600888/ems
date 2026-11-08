import { request } from './http'

export type HistoryPointType = 'yc' | 'yx'
export interface HistorySample {
  point_id: string
  timestamp: number
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
