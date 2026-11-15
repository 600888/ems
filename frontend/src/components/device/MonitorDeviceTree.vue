<script setup lang="ts">
import DeviceScopeTree from './DeviceScopeTree.vue'
import { useMonitorStore } from '@/stores/monitor'
import type { DeviceNode } from '@/types/domain'
const monitor = useMonitorStore()
function select(node: DeviceNode) {
  if (node.isDevice || node.kind === 'component') void monitor.selectDevice(node.id)
}
</script>
<template>
  <DeviceScopeTree
    v-model:keyword="monitor.keyword"
    :tree="monitor.tree"
    :model-value="monitor.selectedId"
    :expanded-keys="monitor.expandedKeys"
    @select="select"
  >
    <template #toolbar
      ><span class="self-start rounded bg-sel px-2 py-1 text-small text-accent"
        >全部 {{ monitor.counts.all }}</span
      ></template
    >
    <template #footer>
      <div class="flex items-center gap-3 py-0.5">
        <span
          v-for="item in [
            { label: '在线', tone: 'text-ok' },
            { label: '告警', tone: 'text-warn' },
            { label: '离线', tone: 'text-subtle' },
          ]"
          :key="item.label"
          class="flex items-center gap-1.5 text-caption text-muted"
          ><i class="dot" :class="item.tone" />{{ item.label }}</span
        >
      </div>
      <p class="text-caption text-subtle">站 → 单元 → 设备 → 部件</p>
    </template>
  </DeviceScopeTree>
</template>
