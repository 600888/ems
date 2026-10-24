/** 设备节点类型，对应后端 cfg_device.node_kind */
export type NodeKind = 'station' | 'unit' | 'device' | 'component'

/** 运行状态：在线 / 告警 / 离线 / 未配置 */
export type DeviceStatus = 'online' | 'alarm' | 'offline' | 'unconfigured'

/** 数据质量 */
export type Quality = 'good' | 'timeout' | 'invalid'

/** 左侧层级树节点 */
export interface DeviceNode {
  id: string
  name: string
  code: string
  kind: NodeKind
  /** lucide 图标名，与设计图一致 */
  icon: string
  status?: DeviceStatus
  /** 节点右侧统计数字 */
  count?: number
  children?: DeviceNode[]
  /** 是否为可跳转的设备（非层级节点） */
  isDevice?: boolean
}

/** 设备列表行（画板 01）：只放配置字段，运行数据在设备监视查看 */
export interface DeviceRow {
  id: string
  name: string
  vendor: string
  type: string
  code: string
  /** 固有点位，如 26 / 28 */
  points: string
  version: string
  /** 版本附加态：生效 / 有草稿 */
  versionState: 'applied' | 'draft'
  actions: string[]
}

/** 实时指标卡（前台核心指标） */
export interface Metric {
  label: string
  value: string
  unit?: string
  note: string
  tone: 'accent' | 'ink' | 'ok' | 'warn' | 'danger'
}

/** 遥测点 */
export interface YcPoint {
  code: string
  name: string
  value: string
  unit: string
  quality: Quality
  ts: string
  group: string
  /** 趋势采样，用于展开曲线 */
  series?: number[]
}

/** 遥信点 */
export interface YxSignal {
  name: string
  state: string
  raw: '0' | '1'
  lastChange: string
  group: string
  level: 'normal' | 'warn' | 'danger' | 'unknown'
}

/** 变位记录 */
export interface YxChange {
  name: string
  time: string
  from: string
  to: string
  scope: string
}

/** 遥控点 */
export interface YkCommand {
  name: string
  desc: string
  current: string
  action: string
  disabledReason?: string
}

/** 控制记录 */
export interface ControlLog {
  time: string
  command: string
  target: string
  operator: string
  result: 'success' | 'failed' | 'pending'
}

/** 遥调点 */
export interface YtPoint {
  name: string
  current: string
  modified: string
  unit: string
  min: number
  max: number
}

/** 点表导入预览行 */
export interface ImportRow {
  row: number
  name: string
  access: string
  type: string
  unit: string
  result: 'pass' | 'warn'
}

/** 解析检查问题 */
export interface ParseIssue {
  title: string
  desc: string
}

/** 固有点位映射行 */
export interface MappingRow {
  name: string
  code: string
  kind: string
  unit: string
  state: 'enabled' | 'disabled'
  stateReason?: string
  source: string
  transform: string
  feedback: string
  passed: boolean
  category: 'yc' | 'yx' | 'yk' | 'yt' | 'event'
}

/** 配置差异行 */
export interface DiffRow {
  type: '新增' | '修改' | '禁用' | '删除'
  item: string
  current: string
  next: string
  impact: string
}

/** 通道绑定行（画板 03）：配置态，不表示通道运行健康度 */
export interface BindingRow {
  name: string
  resource: string
  role: string
  protocol: string
  profile: string
  address: string
  /** 配置状态：启用 / 停用 */
  state: 'enabled' | 'disabled'
  version: string
}

/** 层级详情子节点行（画板 08）：配置字段 */
export interface SubNodeRow {
  name: string
  path: string
  cells: string
  /** 映射点位，如 32 / 32 */
  mapped: string
  /** 点表模板 */
  template: string
  /** 配置状态：生效 / 有草稿 */
  versionState: 'applied' | 'draft'
  source: string
}

/** 告警提示 */
export interface ToastItem {
  id: number
  tone: 'ok' | 'warn' | 'danger' | 'info'
  text: string
}
