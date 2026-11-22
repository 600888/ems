import assert from 'node:assert/strict'
import { after, before, beforeEach, test } from 'node:test'
import { createServer } from 'vite'
import { createPinia, setActivePinia } from 'pinia'

let server, useAlarmStore, useAuthStore, domain, originalFetch, originalWindow
const response = (data) => Response.json({ data, meta: {} })
const row = (id, changes = {}) => ({
  alarm_id: id,
  alarm_code: 'TEMP_HIGH',
  name: '电池温度高',
  severity: 'critical',
  device_id: 'device',
  device_name: 'BMS 01',
  device_path: '站 / 单元',
  occurred_at_ms: Date.now() - 60000,
  cleared_at_ms: null,
  acknowledgement: null,
  trigger_summary: '温度 ≥ 55 ℃',
  ...changes,
})
const detail = (id, changes = {}) => ({ ...row(id), events: [], ...changes })
const page = (items) => ({
  items,
  total: items.length,
  summary: {
    total: items.length,
    active: items.filter((item) => item.cleared_at_ms == null).length,
    cleared: items.filter((item) => item.cleared_at_ms != null).length,
    unacknowledged: items.filter((item) => !item.acknowledgement).length,
    critical: items.length,
    warning: 0,
    info: 0,
  },
  device_counts: { device: items.length },
  queried_at_ms: Date.now(),
})
const session = (permissions = ['alarm.read', 'alarm.ack']) => ({
  kind: 'full',
  user: { user_id: 'operator', role_code: 'operator' },
  permissions,
  csrf_token: 'csrf',
  scope_mode: 'all',
  scopes: [],
})
before(async () => {
  process.env.EMS_IDENTITY_MOCK = 'false'
  server = await createServer({ server: { middlewareMode: true }, logLevel: 'silent' })
  ;({ useAlarmStore } = await server.ssrLoadModule('/src/stores/alarm.ts'))
  ;({ useAuthStore } = await server.ssrLoadModule('/src/stores/auth.ts'))
  domain = await server.ssrLoadModule('/src/domain/alarm.ts')
  originalFetch = globalThis.fetch
  originalWindow = globalThis.window
  globalThis.window = { setTimeout, clearTimeout, dispatchEvent() {} }
})
beforeEach(() => {
  setActivePinia(createPinia())
  useAuthStore().apply(session())
})
after(async () => {
  globalThis.fetch = originalFetch
  globalThis.window = originalWindow
  await server.close()
})

test('北京时间范围、31天限制、确认备注和CSV公式注入防护', () => {
  const now = Date.parse('2026-11-14T17:00:00Z')
  assert.equal(domain.alarmTime(now), '2026-11-15 01:00:00')
  assert.equal(domain.alarmRange(1, now)[0], Date.parse('2026-11-14T16:00:00Z'))
  assert.equal(domain.alarmRange(7, now)[0], now - 7 * 86400000)
  assert.match(domain.alarmFilterError({ view: 'history', start_ms: 2, end_ms: 1 }), /不得晚于/)
  assert.match(
    domain.alarmFilterError({ view: 'history', start_ms: 0, end_ms: 32 * 86400000 }),
    /31 天/,
  )
  assert.match(domain.alarmRemarkError('   '), /填写/)
  assert.match(domain.alarmRemarkError('字'.repeat(501)), /500/)
  const csv = domain.alarmsCsv([row('a', { name: '=HYPERLINK("bad")' })])
  assert.ok(csv.startsWith('\ufeff'))
  assert.ok(csv.includes("'=HYPERLINK"))
  const recovered = row('a', { occurred_at_ms: 0, cleared_at_ms: 90000 })
  assert.equal(domain.alarmDuration(recovered, 999999), '1 分 30 秒')
})

test('查询使用服务端分页和设备子树范围，确认携带CSRF且不修改恢复时间', async () => {
  const store = useAlarmStore(),
    record = row('a')
  let acknowledged = false
  const calls = []
  globalThis.fetch = async (path, options) => {
    const body = JSON.parse(options.body)
    calls.push({ path, body, headers: options.headers })
    if (path.endsWith('acknowledge')) {
      acknowledged = true
      return response({ items: [{ alarm_id: 'a', status: 'acknowledged' }] })
    }
    const current = acknowledged
      ? {
          ...record,
          acknowledgement: {
            user_id: 'u',
            display_name: '张明',
            at_ms: Date.now(),
            remark: '现场检查',
          },
        }
      : record
    return response(path.endsWith('detail') ? { ...current, events: [] } : page([current]))
  }
  await store.applyFilters({ ...store.filters, device_root_id: 'unit' })
  assert.equal(calls[0].body.include_descendants, true)
  assert.equal(calls[0].body.device_root_id, 'unit')
  assert.equal(calls[0].body.page_size, 8)
  await store.openDetail('a')
  assert.equal(await store.acknowledge(['a'], '  现场检查  '), true)
  const write = calls.find((call) => call.path.endsWith('acknowledge'))
  assert.deepEqual(write.body, { alarm_ids: ['a'], remark: '现场检查' })
  assert.equal(write.headers['X-CSRF-Token'], 'csrf')
  assert.equal(store.detail.cleared_at_ms, null)
  assert.equal(store.detail.acknowledgement.display_name, '张明')
  assert.equal(store.result.summary.active, 1)
})

test('较慢旧查询与详情不能覆盖新筛选、新选择', async () => {
  const store = useAlarmStore(),
    pending = []
  globalThis.fetch = (path, options) => new Promise((done) => pending.push({ path, options, done }))
  const old = store.query(),
    current = store.applyFilters({ ...store.filters, keyword: '新查询' })
  assert.equal(pending[0].options.signal.aborted, true)
  pending[0].done(response(page([row('old')])))
  await old
  assert.equal(store.loading, true)
  pending[1].done(response(page([row('new')])))
  await current
  assert.equal(store.result.items[0].alarm_id, 'new')
  const oldDetail = store.openDetail('old'),
    nextDetail = store.openDetail('new')
  pending[2].done(response(detail('old')))
  await oldDetail
  assert.equal(store.detail, null)
  assert.equal(store.detailLoading, true)
  pending[3].done(response(detail('new')))
  await nextDetail
  assert.equal(store.detail.alarm_id, 'new')
})

test('刷新失败保留旧结果并禁止确认；改变筛选后不沿用旧数据', async () => {
  const store = useAlarmStore()
  globalThis.fetch = async () => response(page([row('a')]))
  await store.query()
  globalThis.fetch = async () =>
    Response.json({ error: { reason: 'unavailable', message: '连接中断' } }, { status: 503 })
  await store.query()
  assert.equal(store.result.items[0].alarm_id, 'a')
  assert.match(store.error, /连接中断/)
  assert.match(store.confirmationBlock(['a']), /过期|中断/)
  await store.applyFilters({ ...store.filters, keyword: '其他设备' })
  assert.equal(store.result, null)
})

test('权限与新鲜度检查在操作层执行，不仅依赖按钮隐藏', async () => {
  const store = useAlarmStore()
  let calls = 0
  globalThis.fetch = async () => {
    calls++
    return response(page([row('a')]))
  }
  await store.query()
  store.receivedAt = Date.now() - 16000
  assert.equal(await store.acknowledge(['a'], '检查'), false)
  assert.equal(calls, 1)
  useAuthStore().apply(session(['alarm.read']))
  assert.equal(await store.acknowledge(['a'], '检查'), false)
  assert.match(store.actionError, /权限/)
  useAuthStore().clear()
  await store.query()
  assert.equal(calls, 1)
})

test('已恢复告警仍能独立确认；并发已确认不重复提交成功', async () => {
  const store = useAlarmStore(),
    record = row('a', { cleared_at_ms: Date.now() - 1000 })
  globalThis.fetch = async (path) =>
    response(
      path.endsWith('acknowledge')
        ? { items: [{ alarm_id: 'a', status: 'already_acknowledged' }] }
        : page([record]),
    )
  await store.query()
  assert.equal(await store.acknowledge(['a'], '已恢复并知悉'), true)
  assert.match(store.notice, /成功 0 条.*跳过 1 条/)
  assert.equal(store.result.items[0].cleared_at_ms, record.cleared_at_ms)
})

test('批量部分失败仅保留失败选择，错误备注不被伪装为成功', async () => {
  const store = useAlarmStore()
  globalThis.fetch = async (path) =>
    response(
      path.endsWith('acknowledge')
        ? {
            items: [
              { alarm_id: 'a', status: 'acknowledged' },
              { alarm_id: 'b', status: 'failed', message: '设备范围已变化' },
            ],
          }
        : page([row('a'), row('b')]),
    )
  await store.query()
  store.selectedIds = ['a', 'b']
  assert.equal(await store.acknowledge(['a', 'b'], '已检查'), false)
  assert.deepEqual([...store.selectedIds], ['b'])
  assert.match(store.actionError, /设备范围已变化/)
  assert.equal(store.noticeTone, 'warn')
})

test('超时及不完整回执阻止盲目重试，必须读取服务端确认结果', async () => {
  const store = useAlarmStore()
  let writes = 0
  globalThis.fetch = async (path) => {
    if (path.endsWith('acknowledge')) {
      writes++
      throw new Error('network dropped')
    }
    if (path.endsWith('detail'))
      return response(
        detail('a', {
          acknowledgement: {
            display_name: '李华',
            user_id: 'u',
            at_ms: Date.now(),
            remark: '已检查',
          },
        }),
      )
    return response(page([row('a')]))
  }
  await store.query()
  assert.equal(await store.acknowledge(['a'], '检查'), false)
  assert.deepEqual([...store.unresolvedIds], ['a'])
  await store.acknowledge(['a'], '检查')
  assert.equal(writes, 1)
  await store.resolveAcknowledgements()
  assert.equal(store.unresolvedIds.length, 0)
  assert.match(store.notice, /1 条已确认/)
  globalThis.fetch = async (path) =>
    response(path.endsWith('acknowledge') ? { items: [] } : page([row('a')]))
  await store.acknowledge(['a'], '检查')
  assert.deepEqual([...store.unresolvedIds], ['a'])
})

test('会话与授权变化隔离在途查询、详情和确认响应，普通会话续期保持页面', async () => {
  const store = useAlarmStore(),
    auth = useAuthStore()
  globalThis.fetch = async () => response(page([row('a')]))
  await store.query()
  auth.apply({ ...session(), idle_expires_at: '2030-01-01' })
  assert.equal(store.result.items.length, 1)
  const pending = []
  globalThis.fetch = (path) => new Promise((done) => pending.push({ path, done }))
  const query = store.query(),
    details = store.openDetail('a')
  auth.clear('logout')
  pending.forEach((item) =>
    item.done(response(item.path.endsWith('detail') ? detail('a') : page([row('a')]))),
  )
  await Promise.all([query, details])
  assert.equal(store.result, null)
  assert.equal(store.detail, null)
  auth.apply(session())
  globalThis.fetch = async () => response(page([row('a')]))
  await store.query()
  let finish
  globalThis.fetch = () =>
    new Promise((done) => {
      finish = done
    })
  const ack = store.acknowledge(['a'], '检查')
  auth.apply({
    ...session(),
    scopes: [{ station_id: 'other', device_root_id: null }],
    scope_mode: 'selected',
  })
  finish(response({ items: [{ alarm_id: 'a', status: 'acknowledged' }] }))
  assert.equal(await ack, false)
  assert.equal(store.notice, '')
  assert.equal(store.result, null)
})

test('卸载取消读取，后台完成的确认不会重新启动页面查询', async () => {
  const store = useAlarmStore()
  globalThis.fetch = async () => response(page([row('a')]))
  await store.query()
  let finish,
    calls = 0
  globalThis.fetch = () => {
    calls++
    return new Promise((done) => {
      finish = done
    })
  }
  const ack = store.acknowledge(['a'], '检查')
  store.deactivate()
  finish(response({ items: [{ alarm_id: 'a', status: 'acknowledged' }] }))
  await ack
  assert.equal(calls, 1)
  assert.equal(store.ackBusy, false)
})

test('详情回读可以核实未知确认结果，保留真实恢复状态', async () => {
  const store = useAlarmStore()
  let written = false
  globalThis.fetch = async (path) => {
    if (path.endsWith('acknowledge')) {
      written = true
      throw new Error('response lost after commit')
    }
    const current = row(
      'a',
      written
        ? {
            acknowledgement: {
              display_name: '张明',
              user_id: 'u',
              at_ms: Date.now(),
              remark: '现场检查',
            },
          }
        : {},
    )
    return response(path.endsWith('detail') ? { ...current, events: [] } : page([current]))
  }
  await store.query()
  await store.openDetail('a')
  await store.acknowledge(['a'], '现场检查')
  assert.equal(store.unresolvedIds.length, 0)
  assert.equal(store.detail.acknowledgement.display_name, '张明')
  assert.equal(store.detail.cleared_at_ms, null)
  assert.match(store.notice, /结果已核实/)
})

test('接口未实现时显示服务未接通，非法历史范围不发送请求', async () => {
  const store = useAlarmStore()
  let calls = 0
  globalThis.fetch = async () => {
    calls++
    return Response.json({ error: { reason: 'route_not_found' } }, { status: 404 })
  }
  await store.query()
  assert.match(store.error, /告警服务尚未接通/)
  await store.applyFilters({ ...store.filters, view: 'history', start_ms: 2, end_ms: 1 })
  assert.equal(calls, 1)
  assert.match(store.error, /不得晚于/)
})

test('列表保留归档进度，详情资源不存在时显示真实原因', async () => {
  const store = useAlarmStore()
  globalThis.fetch = async (path) =>
    path.endsWith('detail')
      ? Response.json({ error: { reason: 'alarm_not_found' } }, { status: 404 })
      : Response.json({ data: page([row('a')]), meta: { alarm_archive: { pending: 2 } } })
  await store.query()
  assert.equal(store.archivePending, 2)
  await store.openDetail('a')
  assert.match(store.detailError, /告警不存在/)
})

test('导出使用已提交条件、行数上限并忽略退出后的响应', async () => {
  const store = useAlarmStore()
  globalThis.fetch = async () => response(page([row('a')]))
  await store.applyFilters({ ...store.filters, keyword: 'BMS' })
  globalThis.fetch = async (path, options) => {
    assert.equal(path, '/api/alarms/export')
    assert.equal(JSON.parse(options.body).max_rows, 10000)
    assert.equal(JSON.parse(options.body).keyword, 'BMS')
    return response({ items: [row('a')], truncated: true })
  }
  assert.equal((await store.exportRows()).length, 1)
  assert.match(store.notice, /上限/)
  let finish
  globalThis.fetch = () =>
    new Promise((done) => {
      finish = done
    })
  const exporting = store.exportRows()
  useAuthStore().clear()
  finish(response({ items: [row('a')], truncated: false }))
  assert.equal(await exporting, null)
  assert.equal(store.exporting, false)
})
