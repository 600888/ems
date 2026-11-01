export class ApiError extends Error {
  constructor(
    public status: number,
    public reason: string,
    message: string,
    public effect = 'NotApplied',
    public retryAfter = 0,
    public details: unknown = undefined,
  ) {
    super(message)
    this.name = 'ApiError'
  }
}

let csrfToken = ''
const reasonMessages: Record<string, string> = {
  origin_not_allowed: '请求来源与后端配置不一致，请检查 Web 的 allowed_origins。',
  config_revision_mismatch: '配置已被其他操作修改，请重新加载后再保存。',
  realtime_service_unavailable: '实时库尚未就绪，请检查实时库及采集服务。',
  history_service_unavailable: '历史查询服务尚未配置或未就绪。',
  control_service_unavailable: '遥控、遥调服务尚未接通。',
  invalid_realtime_sample: '实时样本格式不正确，请检查采集服务。',
  device_is_referenced: '设备仍被引用，无法删除。',
  import_confirmation_required: '请先修正并确认导入行，再应用点表。',
  configuration_invalid: '配置校验未通过，请检查通道、点位和依赖关系。',
}
export function setCsrfToken(value: string) {
  csrfToken = value
}

export interface ApiResponse<T> {
  data: T
  meta: { config_revision?: string; request_id?: string; [key: string]: unknown }
}

export async function requestResponse<T>(
  path: string,
  body?: object | FormData,
  options: { anonymous?: boolean; signal?: AbortSignal } = {},
): Promise<ApiResponse<T>> {
  const controller = new AbortController()
  const multipart = body instanceof FormData
  const timeout = window.setTimeout(() => controller.abort(), multipart ? 120000 : 12000)
  const abort = () => controller.abort()
  options.signal?.addEventListener('abort', abort, { once: true })
  if (options.signal?.aborted) controller.abort()
  try {
    const response = await fetch(path, {
      method: body === undefined ? 'GET' : 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      signal: controller.signal,
      headers: {
        Accept: 'application/json',
        ...(body === undefined || multipart ? {} : { 'Content-Type': 'application/json' }),
        ...(!options.anonymous && csrfToken ? { 'X-CSRF-Token': csrfToken } : {}),
      },
      ...(body === undefined ? {} : { body: multipart ? body : JSON.stringify(body) }),
    })
    const payload = await response.json().catch(() => null)
    if (!response.ok) {
      const error = payload?.error
      const reason = error?.reason ?? 'service_unavailable'
      if (
        !options.anonymous &&
        response.status === 401 &&
        ['unauthenticated', 'session_expired'].includes(reason)
      ) {
        window.dispatchEvent(new CustomEvent('ems:session-ended', { detail: 'expired' }))
      }
      throw new ApiError(
        response.status,
        reason,
        reasonMessages[reason] ?? error?.message ?? '服务暂时不可用，请稍后重试',
        error?.effect ?? (body && response.status >= 500 ? 'Unknown' : 'NotApplied'),
        Number(response.headers.get('Retry-After') ?? 0),
        error?.details,
      )
    }
    if (!payload || !('data' in payload))
      throw new ApiError(
        502,
        'invalid_response',
        '服务响应异常，请稍后重试',
        body ? 'Unknown' : 'NotApplicable',
      )
    return { data: payload.data as T, meta: payload.meta ?? {} }
  } catch (error) {
    if (error instanceof ApiError) throw error
    if (controller.signal.aborted)
      throw new ApiError(
        504,
        'request_timeout',
        '请求超时，请检查连接后重试',
        body ? 'Unknown' : 'NotApplicable',
      )
    throw new ApiError(
      503,
      'network_unavailable',
      '无法连接服务，请检查网络后重试',
      body ? 'Unknown' : 'NotApplicable',
    )
  } finally {
    window.clearTimeout(timeout)
    options.signal?.removeEventListener('abort', abort)
  }
}

export async function request<T>(
  path: string,
  body?: object | FormData,
  options: { anonymous?: boolean; signal?: AbortSignal } = {},
): Promise<T> {
  return (await requestResponse<T>(path, body, options)).data
}

export function errorMessage(error: unknown) {
  if (error instanceof ApiError && Array.isArray(error.details)) {
    const reasons = error.details.flatMap((detail: unknown) => {
      if (detail && typeof detail === 'object' && 'reason' in detail) return [String(detail.reason)]
      return []
    })
    if (reasons.length) return `${error.message}：${reasons.slice(0, 5).join('；')}`
  }
  return error instanceof Error ? error.message : '操作失败，请稍后重试'
}
