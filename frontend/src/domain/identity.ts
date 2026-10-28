import type { DeviceNode } from '../types/domain.ts'
import type { ResourceScope, RoleCode } from '../types/identity.ts'

export const roles = [
  { code: 'admin', label: '管理员', description: '账号与工程维护', icon: 'shield-check' },
  { code: 'operator', label: '操作员', description: '运行监视与操作', icon: 'sliders-horizontal' },
  { code: 'observer', label: '观察员', description: '只读运行数据', icon: 'eye' },
] as const

export const permissionMatrix = [
  {
    label: '实时监视、历史与曲线',
    permissions: ['monitor.read', 'history.read'],
    roles: ['admin', 'operator', 'observer'],
  },
  {
    label: '查看告警列表与详情',
    permissions: ['alarm.read'],
    roles: ['admin', 'operator', 'observer'],
  },
  { label: '确认告警、填写处置备注', permissions: ['alarm.ack'], roles: ['admin', 'operator'] },
  {
    label: '提交遥控 / 遥调意图',
    permissions: ['command.yk', 'command.yt'],
    roles: ['admin', 'operator'],
  },
  {
    label: '查看设备档案与四遥定义',
    permissions: ['device.read'],
    roles: ['admin', 'operator', 'observer'],
  },
  { label: '查看通道与绑定详情', permissions: ['channel.read'], roles: ['admin', 'operator'] },
  { label: '新增、修改、删除设备', permissions: ['device.write'], roles: ['admin'] },
  {
    label: '配置通道与设备绑定',
    permissions: ['channel.write', 'binding.write'],
    roles: ['admin'],
  },
  {
    label: '配置四遥、导入与应用点表',
    permissions: ['point.write', 'import.write'],
    roles: ['admin'],
  },
  { label: '查看站内用户', permissions: ['user.read'], roles: ['admin'] },
  { label: '创建、编辑、启停与删除用户', permissions: ['user.write'], roles: ['admin'] },
  { label: '分配角色与设备授权范围', permissions: ['user.grant'], roles: ['admin'] },
  { label: '重置他人密码、账号解锁与登录保护', permissions: ['user.security'], roles: ['admin'] },
  { label: '查看与撤销他人会话', permissions: ['session.manage'], roles: ['admin'] },
  { label: '全局审计查询、系统指标', permissions: ['audit.read', 'system.read'], roles: ['admin'] },
] satisfies { label: string; permissions: string[]; roles: RoleCode[] }[]

export function roleLabel(code?: RoleCode) {
  return roles.find((role) => role.code === code)?.label ?? ''
}

export function permissionsForRole(code: RoleCode) {
  return permissionMatrix
    .filter((row) => row.roles.includes(code))
    .flatMap((row) => row.permissions)
}

export function passwordError(password: string, username: string, min = 12, oldPassword?: string) {
  const length = Array.from(password).length
  if (length < min || length > 32) return `密码长度应为 ${min}–32 个字符`
  const classes = [/[A-Z]/, /[a-z]/, /[0-9]/, /[^A-Za-z0-9]/].filter((rule) => rule.test(password))
  if (classes.length < 3) return '大写、小写、数字、特殊符号中至少包含三类'
  if (password.toLowerCase() === username.toLowerCase()) return '密码不能与用户名相同'
  if (oldPassword !== undefined && password === oldPassword) return '新密码不能与当前密码相同'
  if (/^(password|qwerty|123456)/i.test(password)) return '请使用不易猜测的密码'
  return ''
}

export function safeReturnTo(value: unknown) {
  if (typeof value !== 'string' || !/^\/(monitor|admin|profile)(\/|\?|$)/.test(value)) return ''
  if (/[\\\r\n]/.test(value)) return ''
  return value
}

export function formatTime(value: string | null | undefined, short = false) {
  if (!value) return '尚未登录'
  const date = new Date(value)
  if (!Number.isFinite(date.getTime())) return '—'
  return new Intl.DateTimeFormat('zh-CN', {
    ...(short ? {} : { year: 'numeric' as const }),
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
  })
    .format(date)
    .replaceAll('/', '-')
}

export function scopesFromKeys(tree: DeviceNode[], keys: string[]): ResourceScope[] {
  const checked = new Set(keys)
  const result: ResourceScope[] = []
  function visit(node: DeviceNode, station: string) {
    if (checked.has(node.id)) {
      result.push({ station_id: station, device_root_id: node.id === station ? null : node.id })
    } else node.children?.forEach((child) => visit(child, station))
  }
  tree.forEach((station) => visit(station, station.id))
  return result
}

export function scopeLabels(scopes: ResourceScope[], tree: DeviceNode[]): string[] {
  const names = new Map<string, string>()
  function visit(nodes: DeviceNode[]) {
    nodes.forEach((node) => {
      names.set(node.id, node.name)
      visit(node.children ?? [])
    })
  }
  visit(tree)
  return scopes.map(
    (scope) =>
      `${names.get(scope.station_id) ?? scope.station_id} / ${scope.device_root_id ? (names.get(scope.device_root_id) ?? scope.device_root_id) : '全站'}`,
  )
}
