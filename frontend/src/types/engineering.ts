export type PointType = 'yc' | 'yx' | 'yk' | 'yt'
export interface EngineeringRecord {
  [key: string]: unknown
  device_id: string
  name: string
  code: string
  node_kind: 'station' | 'unit' | 'device' | 'component'
  parent_device_id: string
  source_device_id: string
  device_type_code: string
  type_version: string
  enabled: number | boolean
  vendor: string
  model: string
  firmware_version: string
  description: string
  path: string
  inherent_points?: { total: number; enabled: number; disabled: number; unconfigured: number }
  config_completeness?: 'complete' | 'incomplete'
}
export interface Page<T> {
  items: T[]
  total: number
  page: number
  page_size: number
}
export interface Channel {
  channel_id: string
  code: string
  name: string
  protocol: string
  conn_mode: string
  endpoint: string
  enabled: number
  poll_interval_ms: number
  timeout_ms: number
  retry_times: number
  serial: Record<string, unknown>
  description: string
}
export interface Binding {
  binding_id: string
  device_id: string
  channel_id: string
  device_address: string
  profile_version: string
  enabled: number
  can_collect: number
  can_control: number
  can_forward: number
  description: string
  channel: Channel
}
export interface Point {
  [key: string]: unknown
  point_id: string
  owner_device_id: string
  binding_id: string
  code: string
  name: string
  semantic_code: string
  value_type: string
  wire_type: string
  byte_order: string
  unit: string
  enabled: number
  disabled_reason: string
  address: Record<string, unknown>
}
export interface ImportJob {
  import_job_id: string
  device_id: string
  file_name: string
  status: string
  preview_hash: string
  error_message: string
  allowed_operations: string[]
  [key: string]: unknown
}
export interface StationImportInspection {
  station_code: string
  package_station_code: string
  station_code_match: boolean
}
export interface ImportEntry {
  line_no: number
  vendor_name: string
  point_type: PointType
  semantic_code: string
  parse_status: string
  confirm_state: string
  issue_codes: unknown
  [key: string]: unknown
}
export interface Finding {
  severity: string
  reason: string
  message?: string
  device_id: string
  point_id: string
}
