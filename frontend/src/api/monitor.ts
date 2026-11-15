import { request } from './http'
import type { Point, PointType, Page } from '@/types/engineering'
import { historyApi } from './history'
export type { HistorySample } from './history'
export const COMMAND_TIMEOUT_MS = 3000

export function commandReason(reason: string): string {
  if (reason.startsWith('IEC61850 result_unknown:'))
    return `IEC61850 执行结果未知，请核对设备状态，勿重复下发（${reason.slice(25).trim()}）`
  if (reason.startsWith('IEC61850 rejected:'))
    return `IEC61850 设备拒绝命令（${reason.slice(18).trim()}）`
  if (reason.startsWith('IEC61850 not_sent:'))
    return `IEC61850 命令未发送，请检查写入权限、测点类型和剩余期限（${reason.slice(18).trim()}）`
  const messages: Record<string, string> = {
    expired_before_send: '命令在 3 秒内未发出，请检查设备连接后再操作',
    expired_before_acceptance: '命令已超过 3 秒有效期，未发往设备',
    feedback_timeout: '设备已确认指令，但 3 秒内未收到匹配的反馈，请核对设备状态',
    channel_disconnected: '设备通信连接尚未建立',
    control_point_or_protocol_not_available: '控制点或通信通道不可用，请检查设备连接与配置',
    control_route_removed: '控制通道已不可用，请检查设备连接与配置',
    protocol_acknowledged: '设备已返回协议确认，未配置反馈，实际状态尚未核验',
    collector_unreachable: '无法连接采集服务，请检查采集进程及命令端点',
    command_database_unavailable: '命令记录暂时不可用，请检查数据库连接',
    collector_not_ready: '采集服务尚未就绪，请检查采集进程和设备连接',
    control_service_unavailable: '遥控、遥调服务尚未接通，请检查命令端点',
    command_disabled: '目标设备或控制点已停用',
    intent_not_found: '尚未查到命令记录，无法确认采集服务是否受理',
    request_timeout: '3 秒内未确认执行结果，请核对设备状态，勿重复下发',
    'IEC104 command rejected: unknown_information_object_address':
      '设备拒绝命令：信息体地址（IOA）不存在，请核对测点映射',
    'IEC104 command rejected: unknown_common_address':
      '设备拒绝命令：公共地址（CA）不存在，请核对通信绑定',
    'IEC104 command rejected: unsupported_asdu_type':
      '设备拒绝命令：不支持该 ASDU 类型，请核对遥控遥调类型',
    'IEC104 command rejected: unsupported_cause_of_transmission': '设备拒绝命令：不支持该传送原因',
    'IEC104 command negatively confirmed': '设备明确拒绝了该控制命令，请核对控制模式和设备状态',
    engineering_value_out_of_range: '下发值超出测点允许范围',
    raw_value_out_of_range: '下发值不能转换为该测点的协议数值，请检查类型和系数',
    minimum_control_interval: '操作过于频繁，请等待测点最小控制间隔后再操作',
  }
  if (reason.startsWith('Command timeout/disconnection'))
    return '设备在 3 秒内未返回有效协议确认，执行结果未确认，请核对设备状态，勿重复下发'
  if (reason.startsWith('IEC104 activation confirmed; termination'))
    return '设备已返回激活确认，但未收到配置要求的结束确认，请检查 command_require_termination 设置'
  return messages[reason] ?? reason
}
export interface LivePoint {
  definition: Point
  sample: { value: number | string | boolean; timestamp: number; quality_code: number } | null
  quality: 'good' | 'stale' | 'missing' | 'disabled' | 'invalid'
}
export const monitorApi = {
  points: (device_id: string, point_type: PointType, page = 1) =>
    request<Page<LivePoint>>('/api/monitor/points', {
      device_id,
      point_type,
      page,
      page_size: 200,
    }),
  history: (device_id: string, point_ids: string[], start_ms: number, end_ms: number) =>
    historyApi.query({
      device_id,
      point_type: 'yc',
      point_ids,
      start_ms,
      end_ms,
      interval_ms: 0,
    }),
  submit: (
    body: {
      device_id: string
      point_id: string
      point_type: 'yk' | 'yt'
      value: number | boolean
      reason: string
      intent_id: string
      expires_at_ms?: number
    },
    timeoutMs = COMMAND_TIMEOUT_MS,
  ) => request<{ intent_id: string; status: string }>('/api/commands/submit', body, { timeoutMs }),
  query: (device_id: string, intent_id: string, timeoutMs = COMMAND_TIMEOUT_MS) =>
    request<{ intent_id: string; status: string; reason: string; updated_at_ms: number }>(
      '/api/commands/query',
      { device_id, intent_id },
      { timeoutMs },
    ),
}
