<script setup lang="ts">
import { computed } from 'vue'

import AppIcon from '@/components/common/AppIcon.vue'
import { useMonitorStore, type MonitorScope } from '@/stores/monitor'
import type { DeviceNode } from '@/types/domain'

const monitor = useMonitorStore()
const tree = computed(() => monitor.filteredTree)

const scopes: { key: MonitorScope; label: string }[] = [
  { key: 'all', label: '全部' },
  { key: 'alarm', label: '异常' },
  { key: 'offline', label: '离线' },
]

const scopeCount = computed(() => ({
  all: monitor.counts.all,
  alarm: monitor.counts.alarm,
  offline: monitor.counts.offline,
}))

function dotClass(status?: string) {
  if (status === 'alarm') return 'text-warn'
  if (status === 'offline') return 'text-subtle'
  if (status === 'online') return 'text-ok'
  return 'text-subtle'
}

function hasChildren(data: DeviceNode) {
  return Boolean(data.children && data.children.length)
}

function onNodeClick(data: DeviceNode) {
  if (data.isDevice) monitor.selectDevice(data.id)
}
</script>

<template>
  <aside
    class="flex w-[288px] shrink-0 flex-col gap-1 overflow-hidden rounded-[6px] border border-line bg-panel p-4"
  >
    <div class="flex items-center gap-[10px]">
      <h2 class="text-[15px] font-semibold text-ink">设备目录</h2>
      <div class="flex-1" />
      <AppIcon name="list-filter" :size="15" class="text-muted" />
      <AppIcon name="panel-left-close" :size="15" class="text-muted" />
    </div>

    <el-input
      v-model="monitor.keyword"
      class="ctl search-input mt-1"
      placeholder="搜索设备名称 / 编码"
      clearable
    >
      <template #prefix>
        <AppIcon name="search" :size="14" class="text-subtle" />
      </template>
    </el-input>

    <div class="flex items-center gap-[6px]">
      <button
        v-for="scope in scopes"
        :key="scope.key"
        type="button"
        class="rounded-[4px] px-[9px] py-1 text-[11px] transition-colors"
        :class="
          monitor.scope === scope.key
            ? 'bg-sel text-accent'
            : 'bg-surface text-muted hover:text-ink'
        "
        @click="monitor.scope = scope.key"
      >
        {{ scope.label }} {{ scopeCount[scope.key] }}
      </button>
    </div>

    <div class="my-1 h-px w-full bg-line" />

    <div class="min-h-0 flex-1 overflow-y-auto pl-1">
      <el-tree
        class="ems-tree"
        :data="tree"
        node-key="id"
        :props="{ label: 'name', children: 'children' }"
        :indent="13"
        :default-expanded-keys="['ess000001', 'unit01', 'bms01', 'cluster01']"
        :current-node-key="monitor.selectedId"
        highlight-current
        :expand-on-click-node="false"
        @node-click="onNodeClick"
      >
        <template #default="{ node, data }">
          <div class="flex w-full min-w-0 items-center gap-[6px]">
            <button
              v-if="hasChildren(data as DeviceNode)"
              type="button"
              class="flex shrink-0 items-center text-muted"
              @click.stop="node.expanded ? node.collapse() : node.expand()"
            >
              <AppIcon :name="node.expanded ? 'chevron-down' : 'chevron-right'" :size="12" />
            </button>
            <span v-else class="w-3 shrink-0" />
            <AppIcon
              :name="(data as DeviceNode).icon"
              :size="13"
              :class="data.id === monitor.selectedId ? 'text-accent' : 'text-muted'"
            />
            <span
              class="truncate text-[12px]"
              :class="data.id === monitor.selectedId ? 'font-semibold text-ink' : 'text-muted'"
            >
              {{ data.name }}
            </span>
            <div class="flex-1" />
            <i
              v-if="(data as DeviceNode).status"
              class="dot"
              :class="dotClass((data as DeviceNode).status)"
            />
          </div>
        </template>
      </el-tree>
    </div>

    <div class="h-px w-full bg-line" />

    <div class="flex items-center gap-3 py-[2px]">
      <span class="flex items-center gap-[5px] text-[10px] text-muted">
        <i class="dot text-ok" />在线
      </span>
      <span class="flex items-center gap-[5px] text-[10px] text-muted">
        <i class="dot text-warn" />告警
      </span>
      <span class="flex items-center gap-[5px] text-[10px] text-muted">
        <i class="dot text-subtle" />离线
      </span>
    </div>
    <p class="text-[10px] text-subtle">站 → 单元 → 设备 → 部件</p>
  </aside>
</template>
