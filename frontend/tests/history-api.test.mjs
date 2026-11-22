import assert from 'node:assert/strict'
import { after, before, beforeEach, test } from 'node:test'
import { createServer } from 'vite'
import { createPinia, setActivePinia } from 'pinia'

let server, useHistoryStore, useAuthStore, domain, originalFetch, originalWindow
const response = (data) => Response.json({ data, meta: {} })
const point = (id, kind = 'yc', deviceId = 'device') => ({
  pointId: id,
  kind,
  deviceId,
  deviceName: deviceId,
  name: id,
  code: id,
  unit: '',
})
const sample = (id, value, timestamp = 1000, quality_code = 0) => ({
  point_id: id,
  value,
  timestamp,
  quality_code,
})
before(async () => {
  process.env.EMS_IDENTITY_MOCK = 'false'
  server = await createServer({ server: { middlewareMode: true }, logLevel: 'silent' })
  ;({ useHistoryStore } = await server.ssrLoadModule('/src/stores/history.ts'))
  ;({ useAuthStore } = await server.ssrLoadModule('/src/stores/auth.ts'))
  domain = await server.ssrLoadModule('/src/domain/history.ts')
  originalFetch = globalThis.fetch
  originalWindow = globalThis.window
  globalThis.window = { setTimeout, clearTimeout, dispatchEvent() {} }
})
beforeEach(() => {
  setActivePinia(createPinia())
  useAuthStore().apply({ kind: 'full', user: { user_id: 'reader' }, csrf_token: 'test' })
})
after(async () => {
  globalThis.fetch = originalFetch
  globalThis.window = originalWindow
  await server.close()
})

test('完整分页加载测点，保留子设备；并发展开共用请求，失败可重试', async () => {
  const store = useHistoryStore()
  const device = {
    id: 'device',
    name: 'Device',
    code: 'd',
    kind: 'device',
    icon: 'cpu',
    children: [{ id: 'component', name: 'Component', code: 'c', kind: 'component', icon: 'cpu' }],
  }
  store.tree = [{ id: 'empty', kind: 'unit', name: 'Empty' }, device]
  const target = store.tree[1]
  const calls = []
  let fail = true
  globalThis.fetch = async (path, options) => {
    assert.equal(path, '/api/points/list')
    const body = JSON.parse(options.body)
    calls.push(body)
    if (body.point_type === 'yx' && fail)
      return Response.json({ error: { reason: 'unavailable' } }, { status: 503 })
    const count = body.point_type === 'yc' ? (body.page === 1 ? 200 : 1) : 1
    return response({
      items: Array.from({ length: count }, (_, i) => ({
        point_id: `${body.point_type}-${body.page}-${i}`,
        name: '点位',
        code: `p${i}`,
        enabled: false,
      })),
      total: body.point_type === 'yc' ? 201 : 1,
    })
  }
  await assert.rejects(store.ensurePoints(target))
  assert.equal(target.pointsLoaded, undefined)
  assert.equal(target.children[0].id, 'component')
  fail = false
  calls.length = 0
  await Promise.all([store.ensurePoints(target), store.ensurePoints(target)])
  assert.equal(calls.length, 3)
  assert.equal(target.children.length, 203)
  assert.equal(target.children[0].id, 'component')
  assert.equal(target.pointsLoaded, true)
  store.keyword = '点位'
  await store.ensurePoints(store.filteredTree[0])
  assert.equal(calls.length, 3)
  assert.equal(target.children.length, 203)
})

test('五档采样间隔按毫秒提交，默认和重置都是30秒', async () => {
  const store = useHistoryStore()
  store.selected = [point('power')]
  const intervals = ['5s', '30s', '1分钟', '5分钟', '30分钟']
  assert.deepEqual(Object.keys(domain.HISTORY_INTERVAL_MS), intervals)
  assert.equal(store.interval, '30s')
  const calls = []
  globalThis.fetch = async (_path, options) => {
    calls.push(JSON.parse(options.body).interval_ms)
    return response({ items: [], truncated: false })
  }
  for (const interval of intervals) {
    store.interval = interval
    await store.query()
  }
  assert.deepEqual(calls, [5000, 30000, 60000, 300000, 1800000])
  store.resetFilters()
  assert.equal(store.interval, '30s')
})

test('按设备和四遥分类请求30天数据，服务端间隔作用于图表和明细；质量异常断线', async () => {
  const store = useHistoryStore()
  store.selected = [point('power'), point('state', 'yx'), point('other', 'yc', 'device2')]
  store.quickRange = '30d'
  store.interval = '30分钟'
  const calls = []
  globalThis.fetch = async (path, options) => {
    assert.equal(path, '/api/history/query')
    const body = JSON.parse(options.body)
    calls.push(body)
    return response({
      items:
        body.point_type === 'yx'
          ? [sample('state', true), sample('state', false, 2000)]
          : body.device_id === 'device'
            ? [sample('power', 1), sample('power', 999, 2000, 4), sample('power', 3, 3000)]
            : [sample('other', 5)],
      truncated: false,
    })
  }
  await store.query()
  assert.equal(calls.length, 3)
  assert.ok(calls.every((body) => body.end_ms - body.start_ms === 30 * 86400000))
  assert.ok(calls.every((body) => body.interval_ms === 1800000 && body.max_rows === 10000))
  assert.deepEqual(
    calls.map((body) => body.point_type),
    ['yc', 'yx', 'yc'],
  )
  assert.deepEqual(store.chartSeries[0].values, [1, null, 3])
  assert.deepEqual(store.chartSeries[1].values, [1, 0])
  assert.equal(store.tableRows[1].cells[0].text, '—')
  assert.equal(store.tableRows[1].cells[1].text, '0')
  assert.equal(store.tableRows[0].timestamp, 1000)
  assert.equal(store.busy, false)
  assert.equal(store.queryError, '')
})

test('较慢的旧查询不能覆盖新结果或清除新查询的busy状态', async () => {
  const store = useHistoryStore()
  store.selected = [point('power')]
  const resolve = []
  const signals = []
  globalThis.fetch = (path, options) => {
    signals.push(options.signal)
    return new Promise((done) => resolve.push(done))
  }
  const old = store.query()
  const current = store.query()
  assert.equal(signals[0].aborted, true)
  resolve[0](response({ items: [sample('power', 1)], truncated: true }))
  await old
  assert.equal(store.busy, true)
  assert.equal(store.tableRows.length, 0)
  resolve[1](response({ items: [sample('power', 2)], truncated: false }))
  await current
  assert.equal(store.chartSeries[0].values[0], 2)
  assert.equal(store.busy, false)
  assert.equal(store.truncated, false)
})

test('按间隔绘图使用归档时间，缺失时间桶断线，空响应清除旧曲线', async () => {
  const store = useHistoryStore()
  const start = 1700000000000
  const step = 60000
  store.customRange = [start, start + 5 * step]
  store.interval = '1分钟'
  store.selected = [point('power'), point('empty')]
  let items = [
    { ...sample('power', 0, start), sample_timestamp: start + 5000 },
    { ...sample('power', 4, start + step), sample_timestamp: start + step + 5000 },
    { ...sample('power', 9, start + 3 * step), sample_timestamp: start + 3 * step + 5000 },
  ]
  globalThis.fetch = async () => response({ items, truncated: false })
  await store.query()
  assert.deepEqual(store.range, store.customRange)
  assert.deepEqual(store.chartSeries[0].timestamps, [
    start + 5000,
    start + step + 5000,
    start + 2 * step,
    start + 3 * step + 5000,
  ])
  assert.deepEqual(store.chartSeries[0].values, [0, 4, null, 9])
  assert.deepEqual(store.chartSeries[1].values, [])
  assert.deepEqual(
    store.tableRows.map((row) => row.timestamp),
    [start, start + step, start + 3 * step],
  )

  items = []
  await store.query()
  assert.equal(store.hasChartData, false)
  assert.deepEqual(store.tableRows, [])
})

test('定期刷新会话保留正在加载的设备目录以及已完成的查询', async () => {
  const auth = useAuthStore()
  const session = {
    ...auth.session,
    permissions: ['history.read', 'device.read'],
    scope_mode: 'selected',
    scopes: [
      { station_id: 'station', device_root_id: 'device' },
      { station_id: 'station', device_root_id: 'other' },
    ],
  }
  auth.apply(session)
  const store = useHistoryStore()
  let finishTree
  globalThis.fetch = () => new Promise((done) => (finishTree = done))
  const pending = store.load()
  auth.apply({
    ...session,
    permissions: [...session.permissions].reverse(),
    scopes: [...session.scopes].reverse(),
    idle_expires_at: '2030-01-01T00:00:00Z',
  })
  assert.equal(store.loading, true)
  finishTree(
    response({
      items: [{ device_id: 'station', name: 'Station', code: 's', node_kind: 'station' }],
      total: 1,
    }),
  )
  await pending
  store.selected = [point('power')]
  globalThis.fetch = async () => response({ items: [sample('power', 5)], truncated: false })
  await store.query()
  auth.apply({ ...session, idle_expires_at: '2030-01-01T00:01:00Z' })
  assert.equal(store.tree[0].id, 'station')
  assert.equal(store.selected[0].pointId, 'power')
  assert.equal(store.chartSeries[0].values[0], 5)

  auth.apply({ ...session, scopes: [session.scopes[1]] })
  assert.equal(store.tree.length, 0)
  assert.equal(store.selected.length, 0)
  assert.equal(store.tableRows.length, 0)
})

test('账号退出时清空目录并阻止未完成的查询和测点加载写回', async () => {
  const store = useHistoryStore()
  store.tree = [{ id: 'device', name: 'D', kind: 'device', code: 'd', icon: 'cpu' }]
  store.selected = [point('power')]
  const resolve = []
  globalThis.fetch = (path) => new Promise((done) => resolve.push({ path, done }))
  const points = store.ensurePoints(store.tree[0])
  const query = store.query()
  useAuthStore().clear('logout')
  for (const pending of resolve)
    pending.done(
      response(
        pending.path === '/api/history/query'
          ? { items: [sample('power', 8)], truncated: true }
          : { items: [{ point_id: 'power', name: 'P' }], total: 1 },
      ),
    )
  await Promise.all([points, query])
  assert.equal(store.tree.length, 0)
  assert.equal(store.selected.length, 0)
  assert.equal(store.tableRows.length, 0)
  assert.equal(store.busy, false)
  assert.equal(store.lastQueryAt, '—')
})

test('部分失败保留成功数据并提示，变更条件立即取消旧结果和截断状态', async () => {
  const store = useHistoryStore()
  store.selected = [point('power'), point('state', 'yx')]
  globalThis.fetch = async (path, options) =>
    JSON.parse(options.body).point_type === 'yx'
      ? Response.json({ error: { reason: 'history_service_unavailable' } }, { status: 503 })
      : response({ items: [sample('power', 5)], truncated: true })
  await store.query()
  assert.equal(store.chartSeries[0].values[0], 5)
  assert.match(store.queryError, /遥信/)
  assert.equal(store.truncated, true)
  store.interval = '5s'
  assert.equal(store.tableRows.length, 0)
  assert.equal(store.truncated, false)
  assert.equal(store.queryError, '')
  assert.equal(store.lastQueryAt, '—')
})

test('自定义范围校验不发请求，每次快捷查询以新的当前时间为截止', async () => {
  const store = useHistoryStore()
  store.selected = [point('power')]
  const calls = []
  globalThis.fetch = async (path, options) => {
    calls.push(JSON.parse(options.body))
    return response({ items: [], truncated: false })
  }
  for (const range of [
    [1000, 0],
    [0, 31 * 86400000],
    [NaN, 1000],
  ]) {
    store.customRange = range
    await store.query()
    assert.match(store.queryError, /时间范围/)
  }
  assert.equal(calls.length, 0)
  store.customRange = null
  const now = Date.now
  try {
    Date.now = () => 1800000000000
    await store.query()
    Date.now = () => 1800000010000
    await store.query()
  } finally {
    Date.now = now
  }
  assert.equal(calls[1].end_ms - calls[0].end_ms, 10000)
  assert.equal(calls[1].end_ms - calls[1].start_ms, 86400000)
})

test('CSV保留毫秒、转义引号和公式前缀，图表不会把文本或异常质量当作有效数值', () => {
  assert.equal(domain.csvCell('设备,"温度"'), '"设备,""温度"""')
  assert.equal(domain.csvCell('-123.456'), '"-123.456"')
  assert.equal(domain.csvCell('=HYPERLINK("test")'), '"\'=HYPERLINK(""test"")"')
  assert.equal(domain.chartValue(sample('state', false)), 0)
  assert.equal(domain.chartValue(sample('state', true, 0, 1)), null)
  assert.equal(domain.chartValue(sample('text', 'trip')), null)
  assert.equal(domain.chartValue(sample('power', Infinity)), null)
  assert.match(domain.sampleTime(1001), /1970-01-01 .*:01\.001$/)
})
