import { createHash, createHmac, randomBytes, scrypt, timingSafeEqual } from 'node:crypto'
import type { IncomingMessage, ServerResponse } from 'node:http'
import { promisify } from 'node:util'
import type { Plugin } from 'vite'
import { adminTree } from '../src/mocks/devices.ts'
import {
  passwordError,
  permissionsForRole,
  scopesFromKeys,
  scopeLabels,
} from '../src/domain/identity.ts'
import type {
  AuthSession,
  LoginPolicy,
  ManagedUser,
  ResourceScope,
  RoleCode,
} from '../src/types/identity.ts'

const derive = promisify(scrypt)
const demoPassword = 'DemoPass!2026'
const validRoles: RoleCode[] = ['admin', 'operator', 'observer']
type Account = Omit<ManagedUser, 'edit_token'> & {
  password_hash: string
  failed_attempts: number
  temporary_expires: number
}
type Session = {
  id: string
  user_id: string
  csrf: string
  kind: 'full' | 'password_change'
  created: number
  active: number
  auth: number
  expires: number
}
class MockError extends Error {
  constructor(
    public status: number,
    public reason: string,
    message: string,
  ) {
    super(message)
  }
}
function fail(status: number, reason: string, message: string): never {
  throw new MockError(status, reason, message)
}
const digest = (value: string) => createHash('sha256').update(value).digest('hex')
const random = () => randomBytes(32).toString('base64url')
const iso = (time: number) => new Date(time).toISOString()

async function hashPassword(password: string) {
  const salt = randomBytes(16).toString('hex')
  return `${salt}:${((await derive(password, salt, 64)) as Buffer).toString('hex')}`
}
async function verifyPassword(password: string, encoded: string) {
  const [salt, hash] = encoded.split(':')
  const value = (await derive(password, salt!, 64)) as Buffer
  return timingSafeEqual(Buffer.from(hash!, 'hex'), value)
}

/** 只挂载到 Vite 开发服务器；账号、哈希与 Cookie 会话不进入浏览器或生产包。 */
export function identityMock(): Plugin {
  const accounts = new Map<string, Account>()
  const sessions = new Map<string, Session>()
  const sources = new Map<string, { count: number; until: number }>()
  const editSecret = random()
  const policy: LoginPolicy = {
    failed_login_limit: 6,
    lock_minutes: 10,
    idle_timeout_minutes: 10,
    password_min_length: 12,
    login_notice: '仅限授权人员使用。请妥善保管个人账号，离开终端前及时退出。',
  }
  const token = (value: unknown) =>
    createHmac('sha256', editSecret).update(JSON.stringify(value)).digest('hex')
  const publicUser = (user: Account): ManagedUser => {
    const {
      password_hash,
      failed_attempts: _failed,
      temporary_expires: _temporary,
      ...value
    } = user
    return {
      ...value,
      scope_labels: scopeLabels(value.scopes, adminTree),
      edit_token: token([
        value.username,
        value.display_name,
        value.status,
        value.role_code,
        value.scope_mode,
        value.scopes,
        value.must_change_password,
        password_hash,
      ]),
    }
  }
  const publicPolicy = () => ({ ...policy, edit_token: token(policy) })
  const csrfCookie = (req: IncomingMessage) =>
    req.headers.cookie
      ?.split(';')
      .map((v) => v.trim())
      .find((v) => v.startsWith('ems_dev_session='))
      ?.slice('ems_dev_session='.length) ?? ''
  const clearCookie = (res: ServerResponse) =>
    res.setHeader('Set-Cookie', 'ems_dev_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0')
  const revokeAll = (id: string) => {
    for (const [key, value] of sessions) if (value.user_id === id) sessions.delete(key)
  }
  const lookup = (id: unknown) => {
    const user = typeof id === 'string' ? accounts.get(id) : undefined
    if (!user || user.status === 'deleted') fail(404, 'user_not_found', '用户不存在或已删除')
    return user!
  }
  const field = (body: Record<string, unknown>, key: string, max = 128) => {
    const value = body[key]
    if (typeof value !== 'string' || !value.length || Array.from(value).length > max)
      fail(400, 'invalid_argument', `请正确填写${key === 'display_name' ? '姓名' : key}`)
    return value as string
  }
  const displayName = (body: Record<string, unknown>) => {
    const name = field(body, 'display_name', 64).trim()
    if (!name) fail(400, 'invalid_argument', '请输入姓名')
    return name
  }
  const fields = (body: Record<string, unknown>, allowed: string[]) => {
    if (Object.keys(body).some((key) => !allowed.includes(key)))
      fail(400, 'invalid_argument', '请求包含不允许修改的字段')
  }
  const integer = (value: unknown, min: number, max: number) => {
    if (typeof value !== 'number' || !Number.isInteger(value) || value < min || value > max)
      fail(400, 'invalid_argument', `数值应在 ${min}–${max} 之间`)
    return value as number
  }
  const page = <T>(values: T[], body: Record<string, unknown>) => {
    const n = integer(body.page ?? 1, 1, 100000)
    const size = integer(body.page_size ?? 20, 1, 200)
    return {
      items: values.slice((n - 1) * size, n * size),
      total: values.length,
      page: n,
      page_size: size,
    }
  }
  const checkEdit = (user: Account, body: Record<string, unknown>) => {
    if (body.expected_edit_token !== publicUser(user).edit_token)
      fail(409, 'edit_conflict', '该用户已被其他操作修改，请刷新后重新确认')
  }
  const access = (body: Record<string, unknown>) => {
    const role = body.role_code as RoleCode
    if (
      !validRoles.includes(role) ||
      !['all', 'selected'].includes(String(body.scope_mode)) ||
      !Array.isArray(body.scopes) ||
      body.scopes.length > 200
    )
      fail(422, 'invalid_scope', '请选择有效角色和设备范围')
    const selected = body.scopes as ResourceScope[]
    if (body.scope_mode === 'all') {
      if (role !== 'admin' || selected.length)
        fail(422, 'invalid_scope', '仅管理员可使用全部站与设备范围')
      return { role_code: role, scope_mode: 'all' as const, scopes: [] }
    }
    const valid = new Map<string, string>()
    function visit(nodes: typeof adminTree, station: string) {
      for (const node of nodes) {
        valid.set(node.id, station)
        visit(node.children ?? [], station)
      }
    }
    for (const station of adminTree) visit([station], station.id)
    const keys: string[] = []
    for (const scope of selected) {
      if (
        !scope ||
        typeof scope !== 'object' ||
        typeof scope.station_id !== 'string' ||
        !adminTree.some((s) => s.id === scope.station_id)
      )
        fail(422, 'invalid_scope', '授权站不存在')
      const id = scope.device_root_id ?? scope.station_id
      if (typeof id !== 'string' || valid.get(id) !== scope.station_id)
        fail(422, 'invalid_scope', '设备不属于指定站')
      keys.push(id)
    }
    return {
      role_code: role,
      scope_mode: 'selected' as const,
      scopes: scopesFromKeys(adminTree, keys),
    }
  }
  const sessionData = (session: Session): AuthSession => {
    const user = lookup(session.user_id)
    return {
      kind: session.kind,
      next_action: session.kind === 'full' ? 'enter_app' : 'change_password',
      user: {
        user_id: user.user_id,
        username: user.username,
        display_name: user.display_name,
        role_code: user.role_code,
      },
      csrf_token: session.csrf,
      idle_expires_at: iso(
        Math.min(session.active + policy.idle_timeout_minutes * 60000, session.expires),
      ),
      absolute_expires_at: iso(session.expires),
      auth_time: iso(session.auth),
      password_min_length: policy.password_min_length,
      ...(session.kind === 'full'
        ? {
            permissions: permissionsForRole(user.role_code),
            scope_mode: user.scope_mode,
            scopes: user.scopes,
          }
        : {}),
    }
  }
  function current(req: IncomingMessage) {
    const key = digest(csrfCookie(req))
    const session = sessions.get(key)
    if (!session) fail(401, 'unauthenticated', '请登录后继续')
    const user = lookup(session!.user_id)
    if (
      user.status !== 'enabled' ||
      Date.now() >= session!.expires ||
      Date.now() >= session!.active + policy.idle_timeout_minutes * 60000
    ) {
      sessions.delete(key)
      fail(401, 'session_expired', '会话已超时，请重新登录')
    }
    return { session: session!, user, key }
  }
  const changed = (user: Account) => {
    user.updated_at = iso(Date.now())
    revokeAll(user.user_id)
    return publicUser(user)
  }
  const protect = (actor: Account, target: Account, nextRole: RoleCode, nextStatus: string) => {
    if (actor.user_id === target.user_id)
      fail(422, 'self_management_forbidden', '请通过个人中心维护自己的账号')
    if (
      target.role_code === 'admin' &&
      target.status === 'enabled' &&
      (nextRole !== 'admin' || nextStatus !== 'enabled') &&
      [...accounts.values()].filter((u) => u.role_code === 'admin' && u.status === 'enabled')
        .length <= 1
    )
      fail(409, 'last_admin_required', '系统必须保留至少一名已启用的管理员')
  }
  const fresh = (session: Session) => {
    if (Date.now() - session.auth > 5 * 60000)
      fail(403, 'reauthentication_required', '请验证当前密码后重试')
  }
  const ready = (async () => {
    const hash = await hashPassword(demoPassword)
    const fixtures: [string, string, RoleCode, boolean, string | null][] = [
      ['linyuan', '林远', 'admin', true, null],
      ['chenxiao', '陈晓', 'operator', true, 'unit01'],
      ['zhoumin', '周敏', 'observer', true, null],
      ['wangqi', '王琪', 'operator', true, 'unit02'],
      ['liuyan', '刘岩', 'observer', false, ''],
      ['sunhao', '孙皓', 'admin', true, null],
    ]
    for (const [username, display_name, role_code, enabled, root] of fixtures) {
      const id = `usr-${username}`
      accounts.set(id, {
        user_id: id,
        username,
        display_name,
        role_code,
        status: enabled ? 'enabled' : 'disabled',
        scope_mode: role_code === 'admin' ? 'all' : 'selected',
        scopes:
          role_code === 'admin' || root === ''
            ? []
            : [{ station_id: 'ess000001', device_root_id: root }],
        must_change_password: username === 'liuyan',
        locked_until: username === 'wangqi' ? iso(Date.now() + 10 * 60000) : null,
        created_at: iso(Date.now() - 7 * 86400000),
        updated_at: iso(Date.now()),
        last_login_at: username === 'liuyan' ? null : iso(Date.now() - 3600000),
        password_changed_at: iso(Date.now() - 86400000),
        password_hash: hash,
        failed_attempts: 0,
        temporary_expires: Date.now() + 86400000,
      })
    }
    return hash
  })()

  async function handle(req: IncomingMessage, res: ServerResponse, body: Record<string, unknown>) {
    const path = req.url!
    const anonymous = path === '/api/auth/login' || path === '/api/auth/login-options'
    if (req.method === 'POST') {
      if (req.headers.origin !== `http://${req.headers.host}`)
        fail(403, 'csrf_mismatch', '请求来源不合法，请刷新后重试')
      if (!String(req.headers['content-type']).startsWith('application/json'))
        fail(400, 'invalid_argument', '仅支持 JSON 请求')
    } else if (req.method !== 'GET') fail(405, 'method_not_allowed', '请求方法不支持')
    const readOnly = [
      '/api/auth/login-options',
      '/api/auth/session',
      '/api/profile/me',
      '/api/users/capabilities',
      '/api/users/security-settings',
    ].includes(path)
    if ((readOnly && req.method !== 'GET') || (!readOnly && req.method !== 'POST'))
      fail(405, 'method_not_allowed', '请求方法不支持')
    if (path === '/api/auth/login-options' && req.method === 'GET')
      return {
        login_notice: policy.login_notice,
        password_min_length: policy.password_min_length,
        password_max_length: 32,
      }
    if (path === '/api/auth/login' && req.method === 'POST') {
      fields(body, ['username', 'password'])
      const source = req.socket.remoteAddress ?? 'local'
      const rate = sources.get(source) ?? { count: 0, until: 0 }
      if (rate.until <= Date.now()) {
        rate.count = 0
        rate.until = Date.now() + 60000
      }
      sources.set(source, rate)
      if (++rate.count > 30) {
        res.setHeader('Retry-After', '60')
        fail(429, 'rate_limited', '尝试过于频繁，请稍后重试')
      }
      const username = field(body, 'username', 64).trim().toLowerCase()
      const user = [...accounts.values()].find((u) => u.username === username)
      const before = user ? publicUser(user).edit_token : ''
      const valid = await verifyPassword(
        field(body, 'password'),
        user?.password_hash ?? (await ready),
      )
      if (user?.locked_until && Date.parse(user.locked_until) <= Date.now()) {
        user.locked_until = null
        user.failed_attempts = 0
      }
      if (
        !user ||
        !valid ||
        user.status !== 'enabled' ||
        user.locked_until ||
        (user.must_change_password && user.temporary_expires <= Date.now()) ||
        before !== publicUser(user).edit_token
      ) {
        if (
          user &&
          !valid &&
          !user.locked_until &&
          user.status === 'enabled' &&
          ++user.failed_attempts >= policy.failed_login_limit
        )
          user.locked_until = iso(Date.now() + policy.lock_minutes * 60000)
        fail(401, 'invalid_credentials', '账号或密码错误，或账号暂不可用')
      }
      user.failed_attempts = 0
      user.last_login_at = iso(Date.now())
      const existing = [...sessions.entries()]
        .filter(([, s]) => s.user_id === user.user_id)
        .sort((a, b) => a[1].active - b[1].active)
      while (existing.length >= 3) sessions.delete(existing.shift()![0])
      const value = random()
      const time = Date.now()
      const session: Session = {
        id: randomBytes(12).toString('hex'),
        user_id: user.user_id,
        csrf: random(),
        kind: user.must_change_password ? 'password_change' : 'full',
        created: time,
        active: time,
        auth: time,
        expires: time + (user.must_change_password ? 5 * 60000 : 8 * 3600000),
      }
      sessions.delete(digest(csrfCookie(req)))
      sessions.set(digest(value), session)
      res.setHeader('Set-Cookie', `ems_dev_session=${value}; Path=/; HttpOnly; SameSite=Strict`)
      return sessionData(session)
    }
    if (anonymous) fail(405, 'method_not_allowed', '请求方法不支持')
    if (path === '/api/auth/logout' && !sessions.has(digest(csrfCookie(req)))) {
      clearCookie(res)
      return { logged_out: true }
    }
    const { user, session, key } = current(req)
    if (req.method === 'POST' && req.headers['x-csrf-token'] !== session.csrf)
      fail(403, 'csrf_mismatch', '页面校验已失效，请刷新后重试')
    if (path === '/api/auth/session' && req.method === 'GET') return sessionData(session)
    if (path === '/api/auth/logout' || path === '/api/auth/logout-all') {
      if (path.endsWith('-all')) revokeAll(user.user_id)
      else sessions.delete(key)
      clearCookie(res)
      return { logged_out: true }
    }
    if (path === '/api/auth/password/complete' && req.method === 'POST') {
      fields(body, ['old_password', 'new_password'])
      if (session.kind !== 'password_change') fail(403, 'permission_denied', '请从个人中心修改密码')
      const hash = user.password_hash
      const old = field(body, 'old_password')
      if (!(await verifyPassword(old, hash))) fail(422, 'invalid_credentials', '当前临时密码不正确')
      const next = field(body, 'new_password')
      const error = passwordError(next, user.username, policy.password_min_length, old)
      if (error) fail(422, 'password_policy_failed', error)
      const encoded = await hashPassword(next)
      if (!sessions.has(key) || user.password_hash !== hash)
        fail(409, 'edit_conflict', '认证状态已变化，请重新登录')
      user.password_hash = encoded
      user.must_change_password = false
      user.password_changed_at = iso(Date.now())
      changed(user)
      clearCookie(res)
      return { reauthentication_required: true }
    }
    if (session.kind !== 'full') fail(403, 'password_change_required', '请先完成首次密码修改')
    if (path === '/api/auth/reauthenticate') {
      fields(body, ['password'])
      if (!(await verifyPassword(field(body, 'password'), user.password_hash)))
        fail(422, 'invalid_credentials', '当前密码不正确')
      if (!sessions.has(key)) fail(401, 'session_expired', '会话已失效，请重新登录')
      session.auth = Date.now()
      return { authenticated: true }
    }
    if (path === '/api/auth/activity') {
      session.active = Date.now()
      return sessionData(session)
    }
    const listSessions = (id: string) =>
      [...sessions.values()]
        .filter(
          (s) =>
            s.user_id === id &&
            s.expires > Date.now() &&
            s.active + policy.idle_timeout_minutes * 60000 > Date.now(),
        )
        .map((s) => ({
          session_id: s.id,
          client: s.id === session.id ? '当前浏览器 · Windows' : '浏览器 · Windows',
          source_ip: '127.0.0.***',
          created_at: iso(s.created),
          last_interactive_at: iso(s.active),
          idle_expires_at: iso(Math.min(s.active + policy.idle_timeout_minutes * 60000, s.expires)),
          absolute_expires_at: iso(s.expires),
          current: s.id === session.id,
        }))
    const revokeSession = (id: string, selected: unknown, all = false) => {
      let found = false
      for (const [k, s] of sessions)
        if (s.user_id === id && (all || s.id === selected)) {
          sessions.delete(k)
          found = true
          if (s.id === session.id) clearCookie(res)
        }
      if (!found && !all) fail(404, 'session_not_found', '会话不存在或已退出')
      return { revoked: true }
    }
    if (path.startsWith('/api/profile/')) {
      if (path === '/api/profile/me') return publicUser(user)
      if (path === '/api/profile/update') {
        fields(body, ['display_name'])
        user.display_name = displayName(body)
        user.updated_at = iso(Date.now())
        return publicUser(user)
      }
      if (path === '/api/profile/change-password') {
        fields(body, ['old_password', 'new_password'])
        const old = field(body, 'old_password')
        const hash = user.password_hash
        if (!(await verifyPassword(old, hash))) fail(422, 'invalid_credentials', '当前密码不正确')
        const next = field(body, 'new_password')
        const error = passwordError(next, user.username, policy.password_min_length, old)
        if (error) fail(422, 'password_policy_failed', error)
        const encoded = await hashPassword(next)
        if (!sessions.has(key) || user.password_hash !== hash)
          fail(409, 'edit_conflict', '账号状态已变化，请重新登录')
        user.password_hash = encoded
        user.password_changed_at = iso(Date.now())
        changed(user)
        clearCookie(res)
        return { reauthentication_required: true }
      }
      if (path === '/api/profile/sessions/list') return page(listSessions(user.user_id), body)
      if (path === '/api/profile/sessions/revoke') {
        fields(body, ['session_id'])
        return revokeSession(user.user_id, body.session_id)
      }
    }
    if (path.startsWith('/api/users/')) {
      const permission = permissionsForRole(user.role_code)
      const needed = path.includes('sessions')
        ? 'session.manage'
        : path.includes('security-settings') ||
            path.includes('unlock') ||
            path.includes('reset-password')
          ? 'user.security'
          : path.endsWith('set-access')
            ? 'user.grant'
            : /\/(create|update|set-status|delete)$/.test(path)
              ? 'user.write'
              : 'user.read'
      if (!permission.includes(needed)) fail(403, 'permission_denied', '当前角色没有用户管理权限')
      if (path === '/api/users/capabilities')
        return {
          scope_tree: adminTree,
          login_notice: policy.login_notice,
          password_min_length: policy.password_min_length,
          password_max_length: 32,
        }
      if (path === '/api/users/security-settings') return publicPolicy()
      if (path === '/api/users/security-settings/update') {
        fresh(session)
        fields(body, [
          'failed_login_limit',
          'lock_minutes',
          'idle_timeout_minutes',
          'password_min_length',
          'login_notice',
          'expected_edit_token',
        ])
        if (body.expected_edit_token !== token(policy))
          fail(409, 'edit_conflict', '设置已变化，请刷新后重新确认')
        const values = {
          failed_login_limit: integer(body.failed_login_limit, 3, 6),
          lock_minutes: integer(body.lock_minutes, 10, 30),
          idle_timeout_minutes: integer(body.idle_timeout_minutes, 10, 30),
          password_min_length: integer(body.password_min_length, 12, 32),
          login_notice: String(body.login_notice ?? ''),
        }
        if (Array.from(values.login_notice).length > 500)
          fail(422, 'invalid_argument', '通知不能超过 500 个字符')
        Object.assign(policy, values)
        return publicPolicy()
      }
      if (path === '/api/users/list') {
        const all = [...accounts.values()].filter((u) => u.status !== 'deleted')
        const keyword = String(body.keyword ?? '')
          .trim()
          .toLowerCase()
        const matches = all
          .filter(
            (u) =>
              (!keyword || `${u.username} ${u.display_name}`.toLowerCase().includes(keyword)) &&
              (!body.role_code || u.role_code === body.role_code) &&
              (!body.status || u.status === body.status),
          )
          .sort(
            (a, b) =>
              a.created_at.localeCompare(b.created_at) || a.user_id.localeCompare(b.user_id),
          )
        return {
          ...page(matches.map(publicUser), body),
          stats: {
            total: all.length,
            enabled: all.filter((u) => u.status === 'enabled').length,
            disabled: all.filter((u) => u.status === 'disabled').length,
            locked: all.filter((u) => u.locked_until && Date.parse(u.locked_until) > Date.now())
              .length,
          },
        }
      }
      if (path === '/api/users/create') {
        fields(body, [
          'username',
          'display_name',
          'role_code',
          'status',
          'scope_mode',
          'scopes',
          'temporary_password',
        ])
        const username = field(body, 'username', 64).trim().toLowerCase()
        if (!/^[a-z][a-z0-9_.-]{3,63}$/.test(username))
          fail(422, 'invalid_argument', '用户名须为 4–64 位，以字母开头')
        if ([...accounts.values()].some((u) => u.username === username))
          fail(409, 'username_conflict', '用户名已存在或被保留')
        const a = access(body)
        if (a.role_code === 'admin' || body.status === 'enabled') fresh(session)
        if (body.status !== undefined && !['enabled', 'disabled'].includes(String(body.status)))
          fail(400, 'invalid_argument', '账号状态不合法')
        const password = field(body, 'temporary_password')
        const error = passwordError(password, username, policy.password_min_length)
        if (error) fail(422, 'password_policy_failed', error)
        const encoded = await hashPassword(password)
        if ([...accounts.values()].some((u) => u.username === username))
          fail(409, 'username_conflict', '用户名已存在')
        const created: Account = {
          ...a,
          user_id: `usr-${randomBytes(8).toString('hex')}`,
          username,
          display_name: displayName(body),
          status: body.status === 'enabled' ? 'enabled' : 'disabled',
          must_change_password: true,
          locked_until: null,
          created_at: iso(Date.now()),
          updated_at: iso(Date.now()),
          last_login_at: null,
          password_changed_at: null,
          password_hash: encoded,
          failed_attempts: 0,
          temporary_expires: Date.now() + 86400000,
        }
        accounts.set(created.user_id, created)
        res.statusCode = 201
        return publicUser(created)
      }
      const target = lookup(body.user_id)
      if (path === '/api/users/detail') return publicUser(target)
      if (path === '/api/users/sessions/list') return page(listSessions(target.user_id), body)
      if (path === '/api/users/sessions/revoke') {
        fresh(session)
        return revokeSession(target.user_id, body.session_id, body.all === true)
      }
      checkEdit(target, body)
      if (path === '/api/users/update') {
        fields(body, ['user_id', 'display_name', 'expected_edit_token'])
        if (user.user_id === target.user_id)
          fail(422, 'self_management_forbidden', '请通过个人中心修改姓名')
        target.display_name = displayName(body)
        target.updated_at = iso(Date.now())
        return publicUser(target)
      }
      fresh(session)
      if (path === '/api/users/set-access') {
        fields(body, ['user_id', 'role_code', 'scope_mode', 'scopes', 'expected_edit_token'])
        const a = access(body)
        protect(user, target, a.role_code, target.status)
        Object.assign(target, a)
        return changed(target)
      }
      if (path === '/api/users/set-status') {
        fields(body, ['user_id', 'status', 'expected_edit_token'])
        if (!['enabled', 'disabled'].includes(String(body.status)))
          fail(400, 'invalid_argument', '状态不合法')
        protect(user, target, target.role_code, String(body.status))
        target.status = body.status as 'enabled' | 'disabled'
        return changed(target)
      }
      if (path === '/api/users/delete') {
        fields(body, ['user_id', 'expected_edit_token'])
        protect(user, target, target.role_code, 'deleted')
        target.status = 'deleted'
        return changed(target)
      }
      if (path === '/api/users/unlock') {
        fields(body, ['user_id', 'expected_edit_token'])
        protect(user, target, target.role_code, target.status)
        target.locked_until = null
        target.failed_attempts = 0
        target.updated_at = iso(Date.now())
        return publicUser(target)
      }
      if (path === '/api/users/reset-password') {
        fields(body, ['user_id', 'temporary_password', 'expected_edit_token'])
        protect(user, target, target.role_code, target.status)
        const password = field(body, 'temporary_password')
        const error = passwordError(password, target.username, policy.password_min_length)
        if (error) fail(422, 'password_policy_failed', error)
        const encoded = await hashPassword(password)
        checkEdit(target, body)
        target.password_hash = encoded
        target.must_change_password = true
        target.temporary_expires = Date.now() + 86400000
        return changed(target)
      }
    }
    fail(404, 'not_found', '接口不存在')
  }

  return {
    name: 'ems-development-identity',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        if (!/^\/api\/(auth|users|profile)\//.test(req.url ?? '')) {
          next()
          return
        }
        void (async () => {
          res.setHeader('Content-Type', 'application/json; charset=utf-8')
          res.setHeader('Cache-Control', 'no-store')
          try {
            await ready
            if (req.url!.includes('?')) fail(400, 'invalid_argument', '业务参数必须放在 JSON 中')
            const chunks: Buffer[] = []
            let length = 0
            for await (const part of req) {
              const chunk = Buffer.from(part)
              length += chunk.length
              if (length > 65536) fail(413, 'payload_too_large', '请求内容过大')
              chunks.push(chunk)
            }
            const raw = Buffer.concat(chunks).toString('utf8')
            let body: unknown
            try {
              body = raw ? JSON.parse(raw) : {}
            } catch {
              fail(400, 'invalid_argument', 'JSON 格式不正确')
            }
            if (!body || typeof body !== 'object' || Array.isArray(body))
              fail(400, 'invalid_argument', 'JSON 必须是对象')
            const data = await handle(req, res, body as Record<string, unknown>)
            res.end(JSON.stringify({ data, meta: { request_id: randomBytes(8).toString('hex') } }))
          } catch (error) {
            const problem =
              error instanceof MockError
                ? error
                : new MockError(500, 'internal_error', '演示服务发生错误')
            res.statusCode = problem.status
            res.end(
              JSON.stringify({
                error: {
                  code: problem.status === 409 ? 'Conflict' : 'RequestFailed',
                  reason: problem.reason,
                  message: problem.message,
                  effect: 'NotApplied',
                },
                meta: { request_id: randomBytes(8).toString('hex') },
              }),
            )
          }
        })()
      })
    },
  }
}
