import assert from 'node:assert/strict'
import { after, before, test } from 'node:test'
import { createServer } from 'vite'
import { createPinia, setActivePinia } from 'pinia'

let server, base
const password = 'DemoPass!2026'
before(async () => {
  process.env.EMS_IDENTITY_MOCK = 'true'
  server = await createServer({
    cacheDir: 'node_modules/.vite-identity-tests',
    server: { host: '127.0.0.1', port: 0, strictPort: false },
    logLevel: 'silent',
  })
  await server.listen()
  base = `http://127.0.0.1:${server.httpServer.address().port}`
})
after(async () => {
  await server?.close()
})

function client() {
  let cookie = '',
    csrf = ''
  return {
    async call(path, body, overrides = {}) {
      const response = await fetch(base + '/api/' + path, {
        method: body === undefined ? 'GET' : 'POST',
        headers: {
          cookie,
          origin: base,
          'content-type': 'application/json',
          'x-csrf-token': csrf,
          ...overrides.headers,
        },
        ...(body === undefined ? {} : { body: JSON.stringify(body) }),
        ...Object.fromEntries(Object.entries(overrides).filter(([key]) => key !== 'headers')),
      })
      const setCookie = response.headers.get('set-cookie')
      if (setCookie) cookie = setCookie.split(';')[0]
      const json = await response.json()
      if (json.data?.csrf_token) csrf = json.data.csrf_token
      return { status: response.status, ...json, headers: response.headers }
    },
    login(username, secret = password) {
      return this.call('auth/login', { username, password: secret })
    },
  }
}

test('Cookie 登录、CSRF、角色授权和退出会话', async () => {
  const admin = client()
  assert.equal((await admin.call('auth/session')).status, 401)
  assert.equal((await admin.login('linyuan', 'invalid')).status, 401)
  const logged = await admin.login('linyuan')
  assert.equal(logged.data.kind, 'full')
  assert.match(logged.headers.get('set-cookie'), /HttpOnly; SameSite=Strict/)
  assert.ok(logged.data.permissions.includes('user.grant'))
  assert.equal(
    (await admin.call('users/list', {}, { headers: { 'x-csrf-token': 'invalid' } })).status,
    403,
  )
  assert.equal(
    (await admin.call('users/list', {}, { headers: { origin: 'http://elsewhere.invalid' } }))
      .status,
    403,
  )
  assert.equal((await admin.call('users/list')).status, 405)
  assert.equal((await admin.call('users/list', { page: 1, page_size: 20 })).data.total, 6)
  for (const username of ['chenxiao', 'zhoumin']) {
    const account = client(),
      session = await account.login(username)
    assert.equal((await account.call('users/list', {})).status, 403)
    assert.equal(session.data.permissions.includes('command.yk'), username === 'chenxiao')
    assert.equal((await account.call('profile/me')).status, 200)
    await account.call('auth/logout', {})
  }
  await admin.call('auth/logout', {})
  assert.equal((await admin.call('auth/session')).status, 401)
})

test('创建用户后强制首次改密，改密后临时会话及旧密码失效', async () => {
  const admin = client(),
    newcomer = client()
  await admin.login('linyuan')
  const created = await admin.call('users/create', {
    username: 'testnewuser',
    display_name: '测试用户',
    role_code: 'observer',
    status: 'enabled',
    scope_mode: 'selected',
    scopes: [{ station_id: 'ess000001', device_root_id: 'unit02' }],
    temporary_password: password,
  })
  assert.equal(created.status, 201)
  assert.equal((await newcomer.login('testnewuser')).data.kind, 'password_change')
  assert.equal((await newcomer.call('profile/me')).error.reason, 'password_change_required')
  assert.equal(
    (
      await newcomer.call('auth/password/complete', {
        old_password: password,
        new_password: 'weak',
      })
    ).status,
    422,
  )
  assert.equal(
    (
      await newcomer.call('auth/password/complete', {
        old_password: password,
        new_password: 'ChangedPass!2026',
      })
    ).status,
    200,
  )
  assert.equal((await newcomer.call('auth/session')).status, 401)
  assert.equal((await newcomer.login('testnewuser')).status, 401)
  assert.equal((await newcomer.login('testnewuser', 'ChangedPass!2026')).data.kind, 'full')
  const detail = (await admin.call('users/detail', { user_id: created.data.user_id })).data
  const updated = await admin.call('users/set-access', {
    user_id: detail.user_id,
    expected_edit_token: detail.edit_token,
    role_code: 'operator',
    scope_mode: 'selected',
    scopes: [],
  })
  assert.equal(updated.status, 200)
  assert.equal((await newcomer.call('auth/session')).status, 401)
  assert.equal(
    (
      await admin.call('users/update', {
        user_id: detail.user_id,
        display_name: '过期编辑',
        expected_edit_token: detail.edit_token,
      })
    ).error.reason,
    'edit_conflict',
  )
  assert.equal(
    (
      await admin.call('users/set-access', {
        user_id: detail.user_id,
        expected_edit_token: updated.data.edit_token,
        role_code: 'operator',
        scope_mode: 'all',
        scopes: [],
      })
    ).status,
    422,
  )
  const self = (await admin.call('profile/me')).data
  assert.equal(
    (
      await admin.call('users/set-status', {
        user_id: self.user_id,
        status: 'disabled',
        expected_edit_token: self.edit_token,
      })
    ).error.reason,
    'self_management_forbidden',
  )
  assert.equal((await admin.call('profile/update', { display_name: '   ' })).status, 400)
  assert.equal((await admin.call('profile/me')).data.display_name, self.display_name)
  assert.equal(
    (await admin.call('profile/update', { display_name: '越权', role_code: 'observer' })).status,
    400,
  )
  await admin.call('auth/logout', {})
})

test('停用撤销会话，登录保护策略有范围与冲突校验', async () => {
  const admin = client(),
    another = client()
  await admin.login('linyuan')
  await another.login('sunhao')
  const sun = (await admin.call('users/detail', { user_id: 'usr-sunhao' })).data
  assert.equal(
    (
      await admin.call('users/set-status', {
        user_id: sun.user_id,
        status: 'disabled',
        expected_edit_token: sun.edit_token,
      })
    ).status,
    200,
  )
  assert.equal((await another.call('auth/session')).status, 401)
  const lin = (await admin.call('profile/me')).data
  const enabledSun = await admin.call('users/detail', { user_id: sun.user_id })
  assert.equal(
    (
      await admin.call('users/set-status', {
        user_id: sun.user_id,
        status: 'enabled',
        expected_edit_token: enabledSun.data.edit_token,
      })
    ).status,
    200,
  )
  const policy = (await admin.call('users/security-settings')).data
  const { edit_token, ...values } = policy
  assert.equal(
    (
      await admin.call('users/security-settings/update', {
        ...values,
        failed_login_limit: 7,
        expected_edit_token: edit_token,
      })
    ).status,
    400,
  )
  assert.equal(
    (
      await admin.call('users/security-settings/update', {
        ...values,
        expected_edit_token: 'stale',
      })
    ).status,
    409,
  )
  assert.equal(lin.role_code, 'admin')
  await admin.call('auth/logout', {})
})

test('授权子树保留导航祖先，不能通过相同前缀或其他站越界', async () => {
  const { scopedDeviceTree } = await server.ssrLoadModule('/src/domain/resource-scope.ts')
  const nodes = [
    {
      id: 'station',
      name: '站',
      code: 's',
      kind: 'station',
      icon: 'x',
      count: 100,
      children: [
        {
          id: 'unit01',
          name: '单元一',
          code: 'u1',
          kind: 'unit',
          icon: 'x',
          children: [
            { id: 'device', name: '设备', code: 'd', kind: 'device', icon: 'x', isDevice: true },
          ],
        },
        { id: 'unit010', name: '其他单元', code: 'u2', kind: 'unit', icon: 'x' },
      ],
    },
  ]
  const result = scopedDeviceTree(nodes, 'selected', [
    { station_id: 'station', device_root_id: 'unit01' },
  ])
  assert.deepEqual([...result.allowedIds], ['unit01', 'device'])
  assert.equal(result.tree[0].children.length, 1)
  assert.equal(result.tree[0].count, undefined)
  assert.equal(
    scopedDeviceTree(nodes, 'selected', [{ station_id: 'other', device_root_id: 'unit01' }]).tree
      .length,
    0,
  )
  assert.equal(scopedDeviceTree(nodes, undefined, []).tree.length, 0)
})

test('退出后，延迟返回的会话恢复响应不能重新建立前端登录态', async () => {
  const { useAuthStore } = await server.ssrLoadModule('/src/stores/auth.ts')
  setActivePinia(createPinia())
  const auth = useAuthStore()
  const previousFetch = globalThis.fetch,
    previousWindow = globalThis.window
  let complete
  globalThis.window = { setTimeout, clearTimeout }
  globalThis.fetch = () =>
    new Promise((resolve) => {
      complete = resolve
    })
  try {
    const pending = auth.restore(true)
    auth.clear('logout')
    complete(
      new Response(
        JSON.stringify({
          data: { kind: 'full', user: { user_id: 'old-user' }, csrf_token: 'old-csrf' },
        }),
      ),
    )
    await pending
    assert.equal(auth.session, null)
    assert.equal(auth.endReason, 'logout')
  } finally {
    globalThis.fetch = previousFetch
    globalThis.window = previousWindow
  }
})

test('身份服务临时故障后，可以重试会话检查并恢复到未登录状态', async () => {
  const { useAuthStore } = await server.ssrLoadModule('/src/stores/auth.ts')
  setActivePinia(createPinia())
  const auth = useAuthStore()
  const previousFetch = globalThis.fetch,
    previousWindow = globalThis.window
  let calls = 0
  globalThis.window = { setTimeout, clearTimeout, dispatchEvent() {} }
  globalThis.fetch = async () => {
    const status = ++calls === 1 ? 503 : 401
    return new Response(
      JSON.stringify({ error: { reason: status === 503 ? 'unavailable' : 'unauthenticated' } }),
      { status },
    )
  }
  try {
    await assert.rejects(auth.restore(), { status: 503 })
    assert.equal(auth.initialized, false)
    await auth.restore(true)
    assert.equal(calls, 2)
    assert.equal(auth.initialized, true)
    assert.equal(auth.session, null)
  } finally {
    globalThis.fetch = previousFetch
    globalThis.window = previousWindow
  }
})
