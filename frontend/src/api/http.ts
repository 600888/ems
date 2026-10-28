export class ApiError extends Error {
  constructor(
    public status: number,
    public reason: string,
    message: string,
    public effect = 'NotApplied',
    public retryAfter = 0,
  ) {
    super(message)
    this.name = 'ApiError'
  }
}

let csrfToken = ''
export function setCsrfToken(value: string) {
  csrfToken = value
}

export async function request<T>(
  path: string,
  body?: object,
  options: { anonymous?: boolean; signal?: AbortSignal } = {},
): Promise<T> {
  const controller = new AbortController()
  const timeout = window.setTimeout(() => controller.abort(), 12000)
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
        ...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
        ...(!options.anonymous && csrfToken ? { 'X-CSRF-Token': csrfToken } : {}),
      },
      ...(body === undefined ? {} : { body: JSON.stringify(body) }),
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
        error?.message ?? '服务暂时不可用，请稍后重试',
        error?.effect ?? (body && response.status >= 500 ? 'Unknown' : 'NotApplied'),
        Number(response.headers.get('Retry-After') ?? 0),
      )
    }
    if (!payload || !('data' in payload))
      throw new ApiError(
        502,
        'invalid_response',
        '服务响应异常，请稍后重试',
        body ? 'Unknown' : 'NotApplicable',
      )
    return payload.data as T
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

export function errorMessage(error: unknown) {
  return error instanceof Error ? error.message : '操作失败，请稍后重试'
}
