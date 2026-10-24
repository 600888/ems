import type {DeviceNode, DeviceRow} from '@/types/domain'
import {adminTree, deviceRows} from '@/mocks/devices'

/** 模拟接口耗时，接真实后端时替换为 http 请求 */
const MOCK_DELAY = 160

function resolve<T>(payload: T): Promise<T> {
  return new Promise((done) => setTimeout(() => done(payload), MOCK_DELAY))
}

/** 设备管理相关接口（当前全部走 mock，TODO 对接 /api/devices） */
export const deviceApi = {
  tree: () => resolve<DeviceNode[]>(adminTree),
  list: () => resolve<DeviceRow[]>(deviceRows),
}
