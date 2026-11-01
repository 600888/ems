import { requestResponse } from './http'
import { toRaw } from 'vue'
import type { DeviceNode } from '@/types/domain'
import type {
  EngineeringRecord,
  Page,
  Binding,
  Channel,
  Point,
  PointType,
  ImportJob,
  ImportEntry,
  Finding,
} from '@/types/engineering'

let revision = '',
  sessionEpoch = 0
const revisions = new WeakMap<object, string>()
export const revisionOf = (value: object) => revisions.get(toRaw(value)) ?? ''
async function call<T>(path: string, body?: object | FormData): Promise<T> {
  const owner = sessionEpoch
  const result = await requestResponse<T>(path, body)
  if (owner !== sessionEpoch) throw new Error('账号已切换，请重新加载')
  if (result.meta.config_revision !== undefined) {
    revision = result.meta.config_revision
    const data = result.data
    if (data && typeof data === 'object') {
      revisions.set(data, revision)
      if ('items' in data && Array.isArray(data.items))
        for (const row of data.items)
          if (row && typeof row === 'object') revisions.set(row, revision)
    }
  }
  return result.data
}
function normalizeFlags(body: object): object {
  const flags = new Set(['enabled', 'can_collect', 'can_control', 'can_forward', 'soe', 'invert'])
  return Object.fromEntries(
    Object.entries(body).map(([key, value]) => [
      key,
      flags.has(key)
        ? Boolean(value)
        : value && typeof value === 'object' && !Array.isArray(value)
          ? normalizeFlags(value)
          : value,
    ]),
  )
}
export const engineeringRevision = () => revision
export function clearEngineeringSession() {
  sessionEpoch++
  revision = ''
}
async function write<T>(path: string, body: object, expected: string = revision) {
  if (!expected) throw new Error('请重新加载配置后再保存')
  return call<T>(path, { expected_config_revision: expected, ...normalizeFlags(body) })
}
export const stableId = (prefix: string) => `${prefix}-${crypto.randomUUID()}`
export const nodeIcon = (kind: string) =>
  ({ station: 'building-2', unit: 'boxes', device: 'cpu', component: 'battery' })[kind] ??
  'hard-drive'

async function allPages<T>(path: string, body: object = {}): Promise<Page<T>> {
  const items: T[] = []
  for (let page = 1; page <= 500; page++) {
    const result = await call<Page<T>>(path, { ...body, page, page_size: 200 })
    items.push(...result.items)
    if (page * 200 >= result.total) return { ...result, items }
    if (!result.items.length) throw new Error('分页结果已变化，请重新加载')
  }
  throw new Error('结果过多，请缩小查询范围')
}
export const deviceApi = {
  configuration: () =>
    call<{ station_device_id: string; config_revision: string }>(
      '/api/device-management/configuration',
    ),
  capabilities: () =>
    call<{
      protocols: string[]
      import_formats: string[]
      supported_point_types: PointType[]
      max_upload_bytes: number
    }>('/api/device-management/capabilities'),
  async tree(): Promise<DeviceNode[]> {
    const load = async (parent?: string): Promise<DeviceNode[]> => {
      const items: DeviceNode[] = []
      for (let page = 1; ; page++) {
        const result = await call<
          Page<EngineeringRecord & { has_children: boolean; child_count: number }>
        >('/api/devices/tree', {
          ...(parent ? { parent_device_id: parent } : {}),
          page,
          page_size: 500,
        })
        for (const row of result.items)
          items.push({
            id: row.device_id,
            name: row.name,
            code: row.code,
            kind: row.node_kind,
            icon: nodeIcon(row.node_kind),
            isDevice: row.node_kind === 'device',
            count: row.child_count,
            children: row.has_children ? await load(row.device_id) : [],
          })
        if (page * 500 >= result.total) return items
      }
    }
    return load()
  },
  list: (filters = {}, page = 1, page_size = 200) =>
    call<Page<EngineeringRecord>>('/api/devices/list', { filters, page, page_size }),
  detail: (device_id: string) =>
    call<
      EngineeringRecord & {
        ancestors: EngineeringRecord[]
        source_device: EngineeringRecord | null
      }
    >('/api/devices/detail', { device_id }),
  create: (device_id: string, fields: object, expected?: string) =>
    write<EngineeringRecord>('/api/devices/create', { device_id, fields }, expected),
  update: (device_id: string, fields: object, expected?: string) =>
    write<EngineeringRecord>('/api/devices/update', { device_id, fields }, expected),
  remove: (device_id: string, expected?: string) =>
    write('/api/devices/delete', { device_id }, expected),
  allDevices: () => allPages<EngineeringRecord>('/api/devices/list'),
  types: () => allPages<Record<string, unknown>>('/api/device-types/list'),
  templates: () => allPages<Record<string, unknown>>('/api/device-templates/list'),
  applyTemplate: (body: object, expected?: string) =>
    write('/api/device-templates/apply', body, expected),
  channels: () => allPages<Channel>('/api/channels/list'),
  createChannel: (channel_id: string, fields: object, expected?: string) =>
    write<Channel>('/api/channels/create', { channel_id, fields }, expected),
  updateChannel: (channel_id: string, fields: object, expected?: string) =>
    write<Channel>('/api/channels/update', { channel_id, fields }, expected),
  removeChannel: (channel_id: string, expected?: string) =>
    write('/api/channels/delete', { channel_id }, expected),
  bindings: (device_id: string) => allPages<Binding>('/api/bindings/list', { device_id }),
  createBinding: (device_id: string, binding_id: string, fields: object, expected?: string) =>
    write<Binding>('/api/bindings/create', { device_id, binding_id, fields }, expected),
  updateBinding: (device_id: string, binding_id: string, fields: object, expected?: string) =>
    write<Binding>('/api/bindings/update', { device_id, binding_id, fields }, expected),
  removeBinding: (device_id: string, binding_id: string, expected?: string) =>
    write('/api/bindings/delete', { device_id, binding_id }, expected),
  points: (device_id: string, point_type: PointType, page = 1, filters = {}) =>
    call<Page<Point>>('/api/points/list', { device_id, point_type, page, page_size: 200, filters }),
  inherent: (device_id: string, point_type: PointType) =>
    allPages<Record<string, unknown>>('/api/inherent-points/list', {
      device_id,
      filters: { point_type },
    }),
  savePoint: (
    device_id: string,
    point_type: PointType,
    point_id: string,
    fields: object,
    create = false,
    expected?: string,
  ) =>
    write<Point>(
      `/api/points/${create ? 'create' : 'update'}`,
      { device_id, point_type, point_id, fields },
      expected,
    ),
  removePoint: (device_id: string, point_type: PointType, point_id: string, expected?: string) =>
    write('/api/points/delete', { device_id, point_type, point_id }, expected),
  imports: (device_id: string) =>
    call<Page<ImportJob>>('/api/point-imports/list', { device_id, page_size: 200 }),
  importDetail: (import_job_id: string) =>
    call<ImportJob>('/api/point-imports/detail', { import_job_id }),
  importRows: (import_job_id: string, page = 1) =>
    call<Page<ImportEntry>>('/api/point-imports/rows', { import_job_id, page, page_size: 200 }),
  upload: (file: File, metadata: object, station = false) => {
    const body = new FormData()
    body.append('file', file)
    body.append('metadata', JSON.stringify(metadata))
    return call<ImportJob>(
      station ? '/api/station-imports/create' : '/api/point-imports/create',
      body,
    )
  },
  updateImportRow: (body: object) => call<ImportJob>('/api/point-imports/update-row', body),
  validateImport: (import_job_id: string, expected_preview_hash: string) =>
    call<ImportJob>('/api/point-imports/validate', { import_job_id, expected_preview_hash }),
  applyImport: (import_job_id: string, expected_preview_hash: string, expected?: string) =>
    write(
      '/api/point-imports/apply',
      { import_job_id, expected_preview_hash, mode: 'merge' },
      expected,
    ),
  validate: (device_id: string) =>
    write<{ valid: boolean; issues: Finding[] }>('/api/device-management/validate', { device_id }),
}
