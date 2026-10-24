/** 后台主导航（画板 01–08 共用） */
export interface NavItem {
  key: string
  label: string
  icon: string
  to?: string
  disabled?: boolean
}

export const adminNav: NavItem[] = [
  {key: 'devices', label: '设备管理', icon: 'hard-drive', to: '/admin/devices'},
  {key: 'comm', label: '通信配置', icon: 'network'},
  {key: 'models', label: '型号与点表', icon: 'layers'},
  {key: 'strategy', label: '策略配置', icon: 'workflow'},
  {key: 'alarm', label: '告警规则', icon: 'bell-ring'},
  {key: 'users', label: '用户与权限', icon: 'users'},
  {key: 'audit', label: '审计日志', icon: 'scroll-text'},
  {key: 'system', label: '系统配置', icon: 'settings'},
]

/** 前台运行模块导航（画板 10–15 共用） */
export const monitorNav: NavItem[] = [
  {key: 'dashboard', label: '运行概览', icon: 'layout-dashboard'},
  {key: 'monitor', label: '设备监视', icon: 'monitor', to: '/monitor/overview'},
  {key: 'energy', label: '能量管理', icon: 'workflow'},
  {key: 'power', label: '功率控制', icon: 'sliders-horizontal'},
  {key: 'alarm', label: '告警中心', icon: 'bell-ring'},
  {key: 'history', label: '历史数据', icon: 'chart-no-axes-combined'},
]
