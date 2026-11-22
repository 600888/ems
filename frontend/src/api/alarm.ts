import { ApiError, requestResponse } from './http'
import type {
  AlarmAckResult,
  AlarmDetail,
  AlarmFilters,
  AlarmPage,
  AlarmRecord,
} from '@/types/alarm'

async function call<T>(path: string, body: object, signal?: AbortSignal) {
  try {
    return await requestResponse<T>(`/api/alarms/${path}`, body, { signal })
  } catch (error) {
    if (
      error instanceof ApiError &&
      (error.status === 501 || (error.status === 404 && error.reason === 'route_not_found'))
    )
      throw new ApiError(
        error.status,
        'alarm_service_unavailable',
        '告警服务尚未接通，请联系管理员检查服务配置。',
        error.effect,
        error.retryAfter,
        error.details,
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
  detail: async (id: string, signal?: AbortSignal) =>
    (await call<AlarmDetail>('detail', { alarm_id: id }, signal)).data,
  acknowledge: async (ids: string[], remark: string) =>
    (await call<AlarmAckResult>('acknowledge', { alarm_ids: ids, remark })).data,
  export: async (filters: AlarmFilters, signal?: AbortSignal) =>
    (
      await call<{ items: AlarmRecord[]; truncated: boolean }>(
        'export',
        { ...filters, include_descendants: true, max_rows: 10000 },
        signal,
      )
    ).data,
}
