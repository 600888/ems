import assert from 'node:assert/strict'
import { after, before, beforeEach, test } from 'node:test'
import { createServer } from 'vite'
import { createPinia, setActivePinia } from 'pinia'

let server, useMonitorStore, useAuthStore, originalFetch, originalWindow
const response = (data) => Response.json({ data, meta: {} })
const row = (id, current = '—') => ({
  pointId: id,
  name: '功率设定',
  current,
  modified: current,
  unit: 'kW',
  min: -100,
  max: 100,
})

before(async () => {
  process.env.EMS_IDENTITY_MOCK = 'false'
  server = await createServer({ server: { middlewareMode: true }, logLevel: 'silent' })
  ;({ useMonitorStore } = await server.ssrLoadModule('/src/stores/monitor.ts'))
  ;({ useAuthStore } = await server.ssrLoadModule('/src/stores/auth.ts'))
  originalFetch = globalThis.fetch
  originalWindow = globalThis.window
  globalThis.window = { setTimeout, clearTimeout, dispatchEvent() {} }
})

beforeEach(() => {
  setActivePinia(createPinia())
  useAuthStore().apply({
    kind: 'full',
    user: { user_id: 'operator' },
    csrf_token: 'test',
    permissions: ['monitor.read', 'command.yt'],
  })
})

after(async () => {
  globalThis.fetch = originalFetch
  globalThis.window = originalWindow
  await server.close()
})

test('未修改的无样本遥调点不阻断有效改值，同名点只下发选中的 ID', async () => {
  const store = useMonitorStore()
  store.selectedId = 'device'
  store.ytRows = [row('power'), row('other')]
  assert.match(store.setpointSubmitDisabledReason, /请先修改/)
  store.setYtValue('power', '42')
  assert.equal(store.ytRows[1].modified, '—')
  assert.equal(store.setpointSubmitDisabledReason, '')
  assert.equal(store.changedSetpoints.length, 1)

  const submissions = []
  globalThis.fetch = async (path, options) => {
    const body = JSON.parse(options.body)
    if (path === '/api/commands/submit') {
      submissions.push(body)
      return response({ intent_id: body.intent_id, status: 'accepted' })
    }
    assert.equal(path, '/api/commands/query')
    return response({ intent_id: body.intent_id, status: 'Succeeded' })
  }
  await store.submitSetpoints('调整功率')
  assert.equal(submissions.length, 1)
  assert.equal(submissions[0].point_id, 'power')
  assert.equal(submissions[0].value, 42)
  store.resetYtValue('power')
  assert.equal(store.ytRows[0].modified, '—')
  assert.equal(store.changedSetpoints.length, 0)
})

test('空值、非法值和越界值在请求前被拒绝，其他未修改点不参与校验', async () => {
  const store = useMonitorStore()
  store.ytRows = [row('power'), row('other')]
  let requests = 0
  globalThis.fetch = async () => {
    requests++
    throw new Error('Invalid drafts must never reach the API')
  }
  for (const value of ['', ' ', 'NaN', '101']) {
    store.setYtValue('power', value)
    assert.ok(store.setpointSubmitDisabledReason)
    await assert.rejects(store.submitSetpoints('调整功率'))
  }
  assert.equal(requests, 0)
  store.setYtValue('power', '0')
  assert.equal(store.setpointSubmitDisabledReason, '')
})

test('刷新只保留实际编辑过的值，无样本恢复后不会产生虚假的修改项', async () => {
  const store = useMonitorStore()
  store.selectedId = 'device'
  store.ytRows = [row('power'), row('other')]
  store.setYtValue('power', '42')
  let sampleValue = 10
  globalThis.fetch = async (path, options) => {
    assert.equal(path, '/api/monitor/points')
    const body = JSON.parse(options.body)
    return response({
      items:
        body.point_type === 'yt'
          ? ['power', 'other'].map((id) => ({
              definition: {
                point_id: id,
                name: '功率设定',
                enabled: true,
                unit: 'kW',
                range_min: -100,
                range_max: 100,
              },
              sample: { value: sampleValue, timestamp: Date.now(), quality_code: 0 },
              quality: 'good',
            }))
          : [],
      total: body.point_type === 'yt' ? 2 : 0,
    })
  }
  await store.refresh()
  assert.deepEqual(
    store.ytRows.map((r) => r.modified),
    ['42', '10'],
  )
  assert.deepEqual(
    store.changedSetpoints.map((r) => r.pointId),
    ['power'],
  )
  assert.equal(store.setpointSubmitDisabledReason, '')
  store.resetYtValue('power')
  sampleValue = 20
  await store.refresh()
  assert.deepEqual(
    store.ytRows.map((r) => r.modified),
    ['20', '20'],
  )
  assert.equal(store.changedSetpoints.length, 0)
})
