import { request } from './http'
import type { Point, PointType, Page } from '@/types/engineering'
import { historyApi } from './history'
export type { HistorySample } from './history'
export interface LivePoint {
  definition: Point
  sample: { value: number | string | boolean; timestamp: number; quality_code: number } | null
  quality: 'good' | 'stale' | 'missing' | 'disabled' | 'invalid'
}
export const monitorApi = {
  points: (device_id: string, point_type: PointType, page = 1) =>
    request<Page<LivePoint>>('/api/monitor/points', {
      device_id,
      point_type,
      page,
      page_size: 200,
    }),
  history: (device_id: string, point_ids: string[], start_ms: number, end_ms: number) =>
    historyApi.query({
      device_id,
      point_type: 'yc',
      point_ids,
      start_ms,
      end_ms,
      interval_ms: 0,
    }),
  submit: (body: {
    device_id: string
    point_id: string
    point_type: 'yk' | 'yt'
    value: number | boolean
    reason: string
    intent_id: string
  }) => request<{ intent_id: string; status: string }>('/api/commands/submit', body),
}
