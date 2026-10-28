import type { DeviceNode } from '@/types/domain'
import type { ResourceScope } from '@/types/identity'

/** 按实际父子关系裁剪授权子树，仅保留用于导航的祖先。 */
export function scopedDeviceTree(
  nodes: DeviceNode[],
  mode: 'all' | 'selected' | undefined,
  scopes: ResourceScope[],
): { tree: DeviceNode[]; allowedIds: Set<string> } {
  const allowedIds = new Set<string>()
  function visit(node: DeviceNode, station: string, inherited = false): DeviceNode | null {
    const allowed =
      inherited ||
      mode === 'all' ||
      scopes.some(
        (scope) => scope.station_id === station && (scope.device_root_id ?? station) === node.id,
      )
    if (allowed) allowedIds.add(node.id)
    const children = (node.children ?? [])
      .map((child) => visit(child, station, allowed))
      .filter((child): child is DeviceNode => child !== null)
    if (!allowed && !children.length) return null
    // 未授权祖先只承担导航，不暴露自身的运行状态或全站统计。
    return {
      ...node,
      children,
      count: undefined,
      ...(!allowed ? { isDevice: false, status: undefined } : {}),
    }
  }
  return {
    tree: nodes
      .map((node) => visit(node, node.id))
      .filter((node): node is DeviceNode => node !== null),
    allowedIds,
  }
}
