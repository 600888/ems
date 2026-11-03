import assert from 'node:assert/strict'
import { after, before, test } from 'node:test'
import { createServer } from 'vite'
import { createPinia, setActivePinia } from 'pinia'
import { reactive } from 'vue'
let server, api, http, originalFetch, originalWindow
before(async () => {
  process.env.EMS_IDENTITY_MOCK = 'false'
  server = await createServer({ server: { middlewareMode: true }, logLevel: 'silent' })
  api = await server.ssrLoadModule('/src/api/device.ts')
  http = await server.ssrLoadModule('/src/api/http.ts')
  originalFetch = globalThis.fetch
  originalWindow = globalThis.window
  globalThis.window = { setTimeout, clearTimeout, dispatchEvent() {} }
})
after(async () => {
  globalThis.fetch = originalFetch
  globalThis.window = originalWindow
  await server.close()
})
const response = (data, revision = '1') =>
  Response.json({ data, meta: { config_revision: revision } })
test('设备树逐层分页读取真实接口并映射稳定 ID', async () => {
  api.clearEngineeringSession()
  const calls = []
  globalThis.fetch = async (path, options) => {
    const body = JSON.parse(options.body)
    calls.push([path, body])
    return response({
      items: body.parent_device_id
        ? [
            {
              device_id: 'pcs',
              name: 'PCS',
              code: 'pcs',
              node_kind: 'device',
              child_count: 0,
              has_children: false,
            },
          ]
        : [
            {
              device_id: 'station',
              name: 'Station',
              code: 's',
              node_kind: 'station',
              child_count: 1,
              has_children: true,
            },
          ],
      total: 1,
    })
  }
  const tree = await api.deviceApi.tree()
  assert.equal(tree[0].children[0].id, 'pcs')
  assert.equal(tree[0].children[0].isDevice, true)
  assert.equal(calls[1][1].parent_device_id, 'station')
  assert.equal(calls[0][0], '/api/devices/tree')
})
test('编辑保留读取时的修订号，后台查询不能覆盖过期表单', async () => {
  api.clearEngineeringSession()
  const calls = []
  globalThis.fetch = async (path, options) => {
    const body = options.body ? JSON.parse(options.body) : undefined
    calls.push([path, body])
    if (path === '/api/devices/detail') return response({ device_id: 'pcs', name: 'old' }, '7')
    if (path === '/api/device-management/configuration')
      return response({ config_revision: '9' }, '9')
    return Response.json(
      { error: { reason: 'config_revision_mismatch', message: 'stale', effect: 'NotApplied' } },
      { status: 409 },
    )
  }
  const row = await api.deviceApi.detail('pcs')
  const expected = api.revisionOf(reactive(row))
  await api.deviceApi.configuration()
  await assert.rejects(
    api.deviceApi.update('pcs', { name: 'new', enabled: 1 }, expected),
    (error) => error.status === 409 && error.effect === 'NotApplied',
  )
  const sent = calls.at(-1)[1]
  assert.equal(sent.expected_config_revision, '7')
  assert.equal(sent.fields.enabled, true)
  assert.equal(calls.filter((c) => c[0] === '/api/devices/update').length, 1)
})
test('上传使用 multipart 文件和 metadata，保留返回修订号', async () => {
  api.clearEngineeringSession()
  let sent
  globalThis.fetch = async (path, options) => {
    sent = { path, ...options }
    return response({ import_job_id: 'imp', status: 'parsed' }, '4')
  }
  const file = new File(['code,name\nx,X'], 'points.csv', { type: 'text/csv' })
  await api.deviceApi.upload(file, {
    device_id: 'pcs',
    binding_id: 'binding',
    source_kind: 'vendor_csv',
  })
  assert.equal(sent.path, '/api/point-imports/create')
  assert.ok(sent.body instanceof FormData)
  assert.equal(sent.headers['Content-Type'], undefined)
  assert.equal(sent.body.get('file').name, 'points.csv')
  assert.deepEqual(JSON.parse(sent.body.get('metadata')), {
    device_id: 'pcs',
    binding_id: 'binding',
    source_kind: 'vendor_csv',
  })
  assert.equal(api.engineeringRevision(), '4')
})
test('HTTP 响应封装保留 meta，账号切换后丢弃旧请求', async () => {
  const resultPromise = Promise.withResolvers()
  globalThis.fetch = () => resultPromise.promise
  const old = api.deviceApi.detail('pcs')
  api.clearEngineeringSession()
  resultPromise.resolve(response({ device_id: 'pcs' }, '99'))
  await assert.rejects(old, /账号已切换/)
  assert.equal(api.engineeringRevision(), '')
  globalThis.fetch = async () => response({ ok: true }, '2')
  const result = await http.requestResponse('/api/example')
  assert.equal(result.meta.config_revision, '2')
})

test('实时服务故障不伪造样本，历史仍可按配置点位查询', async () => {
  const { useAuthStore } = await server.ssrLoadModule('/src/stores/auth.ts')
  const { useMonitorStore } = await server.ssrLoadModule('/src/stores/monitor.ts')
  setActivePinia(createPinia())
  useAuthStore().apply({
    kind: 'full',
    user: { user_id: 'reader' },
    csrf_token: 'test',
    permissions: ['monitor.read', 'history.read'],
  })
  const calls = []
  globalThis.fetch = async (path, options) => {
    const body = options.body ? JSON.parse(options.body) : {}
    calls.push([path, body])
    if (path === '/api/devices/tree')
      return response({
        items: [
          {
            device_id: 'device',
            node_kind: 'device',
            name: 'Device',
            code: 'd',
            has_children: false,
          },
        ],
        total: 1,
      })
    if (path === '/api/devices/detail') return response({ device_id: 'device', name: 'Device' })
    if (path === '/api/monitor/points')
      return Response.json(
        {
          error: {
            reason: 'realtime_service_unavailable',
            effect: 'NotApplicable',
          },
        },
        { status: 503 },
      )
    if (path === '/api/points/list')
      return response({
        items:
          body.point_type === 'yc'
            ? [{ point_id: 'temperature', code: 't', name: '温度', enabled: true }]
            : [],
        total: body.point_type === 'yc' ? 1 : 0,
      })
    if (path === '/api/history/query')
      return response({
        items: [{ point_id: 'temperature', timestamp: 1000, value: 12, quality_code: 0 }],
        truncated: false,
      })
    throw new Error(`Unexpected route ${path}`)
  }
  const monitor = useMonitorStore()
  await monitor.load()
  assert.match(monitor.error, /实时库/)
  assert.equal(monitor.live.yc[0].sample, null)
  assert.equal(monitor.ycList[0].value, '—')
  monitor.toggleYc('t')
  await monitor.queryHistory()
  assert.deepEqual(calls.find(([path]) => path === '/api/history/query')[1].point_ids, [
    'temperature',
  ])
  assert.equal(monitor.history[0].value, 12)
})
