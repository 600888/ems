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
  string_required: '请求缺少字符串字段或字段类型不正确',
  origin_not_allowed: '请求来源与后端配置不一致，请检查 Web 的 allowed_origins。',
  config_revision_mismatch: '配置已被其他操作修改，请重新加载后再保存。',
  realtime_service_unavailable: '实时库尚未就绪，请检查实时库及采集服务。',
  history_service_unavailable: '历史查询服务尚未配置或未就绪。',
  history_bounds: '历史查询条件超出限制：最多 6 个测点、30 天、10000 条结果。',
  history_point_type: '历史查询仅支持遥测和遥信。',
  history_scope_mismatch: '历史样本与授权设备或测点不匹配，请检查存储数据。',
  history_order_mismatch: '历史数据顺序异常，请检查存储服务。',
  invalid_history_sample: '历史样本格式不正确，请检查存储服务。',
  alarm_service_unavailable: '告警数据库尚未就绪，请检查数据库配置。',
  alarm_archive_unavailable: '告警归档库暂不可用，无法读取历史或完整详情。',
  alarm_not_found: '告警不存在或已超出当前设备范围。',
  alarm_query_budget: '告警查询超出处理上限，请缩短时间范围或检查归档积压。',
  invalid_alarm_data: '告警记录异常，请检查告警归档。',
  control_service_unavailable: '遥控、遥调服务尚未接通。',
  invalid_realtime_sample: '实时样本格式不正确，请检查采集服务。',
  device_is_referenced: '设备仍被引用，无法删除。',
  import_confirmation_required: '请先修正并确认导入行，再应用点表。',
  configuration_invalid: '配置校验未通过，请检查通道、点位和依赖关系。',
  unscoped_station_clear: '清空整站配置需要全部设备的管理权限。',
  station_clear_root_required: '清空对象必须是当前站，请刷新后重试。',
  station_clear_confirmation_mismatch: '确认编码与当前站编码不一致，请重新输入。',
  invalid_device_code: '编码仅支持字母、数字、下划线和连字符。',
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
  options: { anonymous?: boolean; signal?: AbortSignal; timeoutMs?: number } = {},
): Promise<ApiResponse<T>> {
  const controller = new AbortController()
  const multipart = body instanceof FormData
  const timeout = window.setTimeout(
    () => controller.abort(),
    options.timeoutMs ?? (multipart ? 120000 : 12000),
  )
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
  options: { anonymous?: boolean; signal?: AbortSignal; timeoutMs?: number } = {},
): Promise<T> {
  return (await requestResponse<T>(path, body, options)).data
}

export function errorMessage(error: unknown) {
  if (error instanceof ApiError && Array.isArray(error.details)) {
    const reasons = error.details.flatMap((detail: unknown) => {
      if (detail && typeof detail === 'object' && 'reason' in detail) return [String(detail.reason)]
      if (
        error.reason === 'string_required' &&
        detail &&
        typeof detail === 'object' &&
        'field' in detail
      )
        return [String(detail.field)]
      return []
    })
    if (reasons.length) return `${error.message}：${reasons.slice(0, 5).join('；')}`
  }
  return error instanceof Error ? error.message : '操作失败，请稍后重试'
}
