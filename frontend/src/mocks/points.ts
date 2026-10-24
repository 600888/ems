import type {ControlLog, Metric, YcPoint, YkCommand, YtPoint, YxChange, YxSignal} from '@/types/domain'

/** 生成一条确定性的平滑趋势（避免每次渲染抖动，仅供 mock 展示） */
export function mockSeries(base: number, amplitude: number, count = 30, phase = 0, drift = 0): number[] {
  return Array.from({length: count}, (_, i) => {
    const t = i / (count - 1)
    const wave = Math.sin(t * Math.PI * 1.6 + phase) * amplitude
    const noise = Math.sin(t * 11.3 + phase * 2.7) * amplitude * 0.16
    return Number((base + wave + noise + drift * t).toFixed(3))
  })
}

export interface DeviceProfile {
  id: string
  name: string
  code: string
  type: string
  vendor: string
  firmware: string
  parentPath: string
  pathLabel: string
  icon: string
  dataSource: string
  status: 'online' | 'alarm' | 'offline'
  metrics: Metric[]
  tabCounts: {telemetry: number; signal: number; control: number; setpoint: number}
}

/** 前台两台示例设备的实时档案，切换设备时标题、指标、测点能力同步切换 */
export const monitorProfiles: Record<string, DeviceProfile> = {
  bms01: {
    id: 'bms01',
    name: 'BMS 01 · 电池管理系统',
    code: 'BMS-ESS01',
    type: 'BMS',
    vendor: '示例厂商 B · BMS-215',
    firmware: 'FW 1.8.2',
    parentPath: '华东示范储能站  /  储能单元 01  /  BMS 01',
    pathLabel: '储能单元 01',
    icon: 'battery-charging',
    dataSource: 'BMS 01',
    status: 'online',
    metrics: [
      {label: 'SOC', value: '72.6', unit: '%', note: '剩余电量', tone: 'accent'},
      {label: 'SOH', value: '98.2', unit: '%', note: '健康状态良好', tone: 'ink'},
      {label: '总电压', value: '1,286.4', unit: 'V', note: '额定 1,331.2 V', tone: 'ink'},
      {label: '总电流', value: '−186.5', unit: 'A', note: '放电中', tone: 'ok'},
      {label: '最高单体温度', value: '34.0', unit: '°C', note: '温差 5.4 °C', tone: 'ink'},
      {label: '活动告警', value: '1', unit: '条', note: '电池簇 02 · 温差预警', tone: 'warn'},
    ],
    tabCounts: {telemetry: 48, signal: 36, control: 4, setpoint: 6},
  },
  pcs01: {
    id: 'pcs01',
    name: 'PCS 01 · 储能变流器',
    code: 'PCS-ESS01',
    type: 'PCS',
    vendor: '示例厂商 A · PCS-125K',
    firmware: 'FW 2.1.6',
    parentPath: '华东示范储能站  /  储能单元 01  /  PCS 01',
    pathLabel: '储能单元 01',
    icon: 'cpu',
    dataSource: 'PCS 01',
    status: 'online',
    metrics: [
      {label: '有功功率', value: '240.0', unit: 'kW', note: '正值放电 · 负值充电', tone: 'accent'},
      {label: '无功功率', value: '0.0', unit: 'kvar', note: '功率因数 1.000', tone: 'ink'},
      {label: '直流电压', value: '1,286.4', unit: 'V', note: '直流侧', tone: 'ink'},
      {label: '交流电压', value: '690.2', unit: 'V', note: '线电压 Uab', tone: 'ink'},
      {label: '运行状态', value: '并网放电', note: '远方控制', tone: 'ok'},
      {label: '活动告警', value: '0', unit: '条', note: '设备运行正常', tone: 'ok'},
    ],
    tabCounts: {telemetry: 32, signal: 24, control: 4, setpoint: 6},
  },
}

/** 画板 10 运行总览的关键遥测（含极值位置） */
export interface KeyTelemetryRow {
  name: string
  value: string
  unit: string
  position: string
  quality: 'good' | 'timeout'
}

export const keyTelemetry: KeyTelemetryRow[] = [
  {name: '最高单体电压', value: '3.362', unit: 'V', position: '簇 01 / 包 04 / #036', quality: 'good'},
  {name: '最低单体电压', value: '3.318', unit: 'V', position: '簇 03 / 包 02 / #012', quality: 'good'},
  {name: '单体电压差', value: '0.044', unit: 'V', position: '—', quality: 'good'},
  {name: '最高单体温度', value: '34.0', unit: '°C', position: '簇 02 / 包 01 / #008', quality: 'good'},
  {name: '最低单体温度', value: '28.6', unit: '°C', position: '簇 01 / 包 02 / #024', quality: 'good'},
  {name: '绝缘电阻', value: '1,560', unit: 'kΩ', position: '正极对地', quality: 'good'},
  {name: '最大允许充电电流', value: '250.0', unit: 'A', position: '—', quality: 'good'},
  {name: '最大允许放电电流', value: '300.0', unit: 'A', position: '—', quality: 'good'},
]

/** 画板 10 遥信摘要 */
export interface SignalSummaryRow {
  label: string
  value: string
  tone: 'ok' | 'accent' | 'warn'
}

export const signalSummary: SignalSummaryRow[] = [
  {label: '运行状态', value: '放电中', tone: 'ok'},
  {label: '主接触器', value: '闭合', tone: 'ok'},
  {label: '控制模式', value: '远方', tone: 'accent'},
  {label: '充电 / 放电允许', value: '允许 / 允许', tone: 'ok'},
  {label: '急停 / 消防联动', value: '未触发 / 未触发', tone: 'ok'},
]

/** 画板 11 遥测点位（BMS） */
export const ycPoints: YcPoint[] = [
  {code: 'soc', name: '系统 SOC', value: '72.6', unit: '%', quality: 'good', ts: '14:32:08', group: '电池系统', series: mockSeries(76, 3.4, 30, 0.4, -3.4)},
  {code: 'soh', name: '系统 SOH', value: '98.2', unit: '%', quality: 'good', ts: '14:32:08', group: '电池系统', series: mockSeries(98.2, 0.2, 30, 1.1)},
  {code: 'total_voltage', name: '电池总电压', value: '1,286.4', unit: 'V', quality: 'good', ts: '14:32:08', group: '电池系统', series: mockSeries(1290, 12, 30, 0.2, -4)},
  {code: 'total_current', name: '电池总电流', value: '−186.5', unit: 'A', quality: 'good', ts: '14:32:08', group: '电池系统', series: mockSeries(-180, 26, 30, 2.2)},
  {code: 'cell_voltage_max', name: '最高单体电压', value: '3.362', unit: 'V', quality: 'good', ts: '14:32:08', group: '电芯电压', series: mockSeries(3.36, 0.01, 30, 0.9)},
  {code: 'cell_voltage_min', name: '最低单体电压', value: '3.318', unit: 'V', quality: 'good', ts: '14:32:08', group: '电芯电压', series: mockSeries(3.32, 0.008, 30, 1.7)},
  {code: 'cell_temp_max', name: '最高单体温度', value: '34.0', unit: '°C', quality: 'good', ts: '14:32:08', group: '温度', series: mockSeries(32.4, 1.8, 30, 0.6, 1.6)},
  {code: 'cell_temp_min', name: '最低单体温度', value: '28.6', unit: '°C', quality: 'good', ts: '14:32:08', group: '温度', series: mockSeries(28.4, 0.5, 30, 2.4)},
  {code: 'insulation_res_pos', name: '正极绝缘电阻', value: '1,560', unit: 'kΩ', quality: 'good', ts: '14:32:08', group: '绝缘', series: mockSeries(1560, 40, 30, 0.3)},
  {code: 'insulation_res_neg', name: '负极绝缘电阻', value: '—', unit: 'kΩ', quality: 'timeout', ts: '14:31:48', group: '绝缘'},
]

/** 画板 12 遥信点位 */
export const yxSignals: YxSignal[] = [
  {name: '充电允许', state: '允许', raw: '1', lastChange: '12:05:16', group: '运行状态', level: 'normal'},
  {name: '放电允许', state: '允许', raw: '1', lastChange: '12:05:16', group: '运行状态', level: 'normal'},
  {name: '主接触器状态', state: '闭合', raw: '1', lastChange: '12:05:18', group: '运行状态', level: 'normal'},
  {name: '远方 / 就地', state: '远方', raw: '1', lastChange: '08:00:00', group: '运行状态', level: 'normal'},
  {name: '单体过压', state: '未触发', raw: '0', lastChange: '—', group: '电压保护', level: 'normal'},
  {name: '单体欠压', state: '未触发', raw: '0', lastChange: '—', group: '电压保护', level: 'normal'},
  {name: '单体温差预警', state: '已触发', raw: '1', lastChange: '14:28:36', group: '温度保护', level: 'warn'},
  {name: '单体过温', state: '未触发', raw: '0', lastChange: '—', group: '温度保护', level: 'normal'},
  {name: '绝缘故障', state: '未触发', raw: '0', lastChange: '—', group: '绝缘监测', level: 'normal'},
  {name: '消防联动', state: '未触发', raw: '0', lastChange: '—', group: '消防', level: 'normal'},
  {name: '急停按钮', state: '未触发', raw: '0', lastChange: '—', group: '安全', level: 'normal'},
  {name: '通信中断', state: '未触发', raw: '0', lastChange: '—', group: '通信', level: 'normal'},
]

export const yxChanges: YxChange[] = [
  {name: '单体温差预警', time: '14:28:36', from: '未触发', to: '已触发', scope: '电池簇 02'},
  {name: '主接触器状态', time: '12:05:18', from: '断开', to: '闭合', scope: 'BMS 01'},
  {name: '放电允许', time: '12:05:16', from: '禁止', to: '允许', scope: 'BMS 01'},
  {name: '远方 / 就地', time: '08:00:00', from: '就地', to: '远方', scope: 'BMS 01'},
]

/** 画板 13 遥控点位 */
export const ykCommands: YkCommand[] = [
  {name: '设备开机', desc: '设备正在运行', current: '当前：运行中', action: '开机', disabledReason: '设备已处于运行状态，无需重复下发'},
  {name: '设备停机', desc: '停止充放电，转入待机', current: '当前：并网放电', action: '停机'},
  {name: '故障复位', desc: '当前无可复位故障', current: '当前：无活动故障', action: '复位', disabledReason: '当前没有可复位的故障'},
  {name: '并离网切换', desc: '运行中不可切换', current: '当前：并网运行', action: '切换', disabledReason: '运行中不允许并离网切换'},
]

export const controlLogs: ControlLog[] = [
  {time: '12:05:16', command: '设备开机', target: '启动', operator: '值班员', result: 'success'},
  {time: '08:12:34', command: '故障复位', target: '复位', operator: '值班员', result: 'success'},
]

/** 画板 14 遥调点位 */
export const ytPoints: YtPoint[] = [
  {name: '有功功率设定', current: '240.0', modified: '200.0', unit: 'kW', min: -500, max: 500},
  {name: '无功功率设定', current: '0.0', modified: '0.0', unit: 'kvar', min: -250, max: 250},
  {name: '功率因数设定', current: '1.000', modified: '1.000', unit: '—', min: -1, max: 1},
  {name: '充电功率限值', current: '500.0', modified: '500.0', unit: 'kW', min: 0, max: 500},
  {name: '放电功率限值', current: '500.0', modified: '500.0', unit: 'kW', min: 0, max: 500},
  {name: '功率变化率', current: '50.0', modified: '50.0', unit: 'kW/s', min: 1, max: 100},
]

/** 画板 08 电池簇指标 */
/** 画板 08 层级节点指标（配置口径：子节点 / 固有点位 / 配置版本） */
export const clusterMetrics: Metric[] = [
  {label: '子节点', value: '4', unit: '个', note: 'Pack 01–04 · 同层同级', tone: 'accent'},
  {label: '固有点位', value: '128', unit: '项', note: '经 bms01 映射', tone: 'ink'},
  {label: '配置版本', value: 'v12', unit: '已生效', note: '生效时间 2026-10-22 13:40', tone: 'ink'},
]

/** 画板 13/14 的 PCS 遥测点位 */
export const pcsYcPoints: YcPoint[] = [
  {code: 'active_power', name: '交流有功功率', value: '240.0', unit: 'kW', quality: 'good', ts: '14:32:08', group: '功率', series: mockSeries(236, 12, 30, 0.5)},
  {code: 'reactive_power', name: '交流无功功率', value: '0.0', unit: 'kvar', quality: 'good', ts: '14:32:08', group: '功率', series: mockSeries(0, 2.4, 30, 1.4)},
  {code: 'power_factor', name: '功率因数', value: '1.000', unit: '—', quality: 'good', ts: '14:32:08', group: '功率', series: mockSeries(0.998, 0.004, 30, 0.8)},
  {code: 'dc_voltage', name: '直流电压', value: '1,286.4', unit: 'V', quality: 'good', ts: '14:32:08', group: '直流侧', series: mockSeries(1288, 9, 30, 0.3)},
  {code: 'dc_current', name: '直流电流', value: '−186.5', unit: 'A', quality: 'good', ts: '14:32:08', group: '直流侧', series: mockSeries(-182, 14, 30, 2.1)},
  {code: 'ac_voltage_ab', name: '交流线电压 Uab', value: '690.2', unit: 'V', quality: 'good', ts: '14:32:08', group: '交流侧', series: mockSeries(690, 2.2, 30, 0.9)},
  {code: 'ac_current_a', name: 'A 相电流', value: '201.4', unit: 'A', quality: 'good', ts: '14:32:08', group: '交流侧', series: mockSeries(199, 6, 30, 1.9)},
  {code: 'igbt_temp', name: 'IGBT 温度', value: '48.6', unit: '°C', quality: 'good', ts: '14:32:08', group: '温度', series: mockSeries(47.2, 1.6, 30, 0.2)},
  {code: 'cabinet_temp', name: '机柜温度', value: '36.2', unit: '°C', quality: 'good', ts: '14:32:08', group: '温度', series: mockSeries(36, 0.9, 30, 1.2)},
  {code: 'efficiency', name: '转换效率', value: '98.7', unit: '%', quality: 'good', ts: '14:32:08', group: '效率', series: mockSeries(98.6, 0.3, 30, 0.7)},
]

/** PCS 遥信 */
export const pcsYxSignals: YxSignal[] = [
  {name: '并网状态', state: '并网运行', raw: '1', lastChange: '12:05:16', group: '运行状态', level: 'normal'},
  {name: '运行模式', state: '远方', raw: '1', lastChange: '08:00:00', group: '运行状态', level: 'normal'},
  {name: '直流侧接触器', state: '闭合', raw: '1', lastChange: '12:05:18', group: '运行状态', level: 'normal'},
  {name: '交流侧接触器', state: '闭合', raw: '1', lastChange: '12:05:18', group: '运行状态', level: 'normal'},
  {name: '模块过温', state: '未触发', raw: '0', lastChange: '—', group: '温度保护', level: 'normal'},
  {name: '直流过压', state: '未触发', raw: '0', lastChange: '—', group: '电压保护', level: 'normal'},
  {name: '交流欠压', state: '未触发', raw: '0', lastChange: '—', group: '电压保护', level: 'normal'},
  {name: '孤岛保护', state: '未触发', raw: '0', lastChange: '—', group: '保护', level: 'normal'},
  {name: '急停按钮', state: '未触发', raw: '0', lastChange: '—', group: '安全', level: 'normal'},
  {name: '消防联动', state: '未触发', raw: '0', lastChange: '—', group: '安全', level: 'normal'},
]
