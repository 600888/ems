import { ApiError, request } from './http'
import type {
  AlarmAckResult,
  AlarmDetail,
  AlarmFilters,
  AlarmPage,
  AlarmRecord,
} from '@/types/alarm'

async function call<T>(path: string, body: object, signal?: AbortSignal) {
  try {
    return await request<T>(`/api/alarms/${path}`, body, { signal })
  } catch (error) {
    if (error instanceof ApiError && [404, 501].includes(error.status))
      throw new ApiError(
        error.status,
        'alarm_service_unavailable',
        '告警服务尚未接通，请联系管理员检查服务配置。',
      )
    throw error
  }
}

export const alarmApi = {
  query: (filters: AlarmFilters, page: number, pageSize: number, signal?: AbortSignal) =>
    call<AlarmPage>(
      'query',
      { ...filters, include_descendants: true, page, page_size: pageSize },
      signal,
    ),
  detail: (id: string, signal?: AbortSignal) =>
    call<AlarmDetail>('detail', { alarm_id: id }, signal),
  acknowledge: (ids: string[], remark: string) =>
    call<AlarmAckResult>('acknowledge', { alarm_ids: ids, remark }),
  export: (filters: AlarmFilters, signal?: AbortSignal) =>
    call<{ items: AlarmRecord[]; truncated: boolean }>(
      'export',
      { ...filters, include_descendants: true, max_rows: 10000 },
      signal,
    ),
}
