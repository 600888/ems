export type AlarmSeverity = 'critical' | 'warning' | 'info'
export type AlarmView = 'active' | 'history'

export interface AlarmAcknowledgement {
  user_id: string
  display_name: string
  at_ms: number
  remark: string
}

/** 恢复与确认彼此独立；确认不得改变 cleared_at_ms。 */
export interface AlarmRecord {
  alarm_id: string
  alarm_code: string
  name: string
  severity: AlarmSeverity
  device_id: string
  device_name: string
  device_path: string
  occurred_at_ms: number
  cleared_at_ms: number | null
  acknowledgement: AlarmAcknowledgement | null
  trigger_summary: string
}

export interface AlarmDetail extends AlarmRecord {
  point_name?: string
  point_id?: string
  trigger_condition?: string
  recovery_condition?: string
  trigger_value?: string
  current_value?: string
  quality?: 'good' | 'stale' | 'invalid' | 'missing'
  sampled_at_ms?: number
  events: {
    event_id: string
    kind: 'occurred' | 'cleared' | 'acknowledged'
    at_ms: number
    detail: string
    display_name?: string
  }[]
}

export interface AlarmFilters {
  view: AlarmView
  device_root_id: string
  severity: AlarmSeverity | ''
  acknowledgement: '' | 'pending' | 'acknowledged'
  recovery: '' | 'active' | 'cleared'
  keyword: string
  start_ms?: number
  end_ms?: number
}

export interface AlarmSummary {
  total: number
  active: number
  cleared: number
  unacknowledged: number
  critical: number
  warning: number
  info: number
}

export interface AlarmPage {
  items: AlarmRecord[]
  total: number
  /** 当前条件的完整结果统计，不是当前页计数。 */
  summary: AlarmSummary
  /** 授权范围内各设备子树的活动告警数，与列表筛选无关。 */
  device_counts: Record<string, number>
  queried_at_ms: number
}

export interface AlarmAckResult {
  items: {
    alarm_id: string
    status: 'acknowledged' | 'already_acknowledged' | 'failed'
    message?: string
  }[]
}
