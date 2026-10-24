<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import { useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import { useDeviceStore } from '@/stores/device'
import type { DeviceNode } from '@/types/domain'

const router = useRouter()
const device = useDeviceStore()
const treeRef = ref()
const keyword = ref('')

// 设备树由 store 异步加载；直接进入子页面时在这里兜底拉取一次
onMounted(() => {
  if (!device.tree.length) device.load()
})

function filterNode(value: string, data: DeviceNode) {
  if (!value) return true
  return data.name.includes(value) || data.code.includes(value)
}

watch(keyword, (value) => {
  treeRef.value?.filter(value)
})

function hasChildren(data: DeviceNode) {
  return Boolean(data.children && data.children.length > 0)
}

function open(data: DeviceNode) {
  device.selectNode(data.id)
  if (data.isDevice) {
    router.push(`/admin/devices/${data.id}/basic`)
    return
  }
  if (data.kind === 'component') {
    router.push(`/admin/hierarchy/${data.id}`)
  }
}

function onNodeClick(
  data: DeviceNode,
  node: { expanded: boolean; expand: () => void; collapse: () => void },
) {
  open(data)
  if (hasChildren(data) && !data.isDevice) {
    node.expanded ? node.collapse() : node.expand()
  }
}
</script>

<template>
  <aside
    class="flex w-[256px] shrink-0 flex-col gap-1 overflow-hidden rounded-[6px] border border-line bg-panel p-[14px]"
  >
    <div class="flex items-center justify-between">
      <h2 class="text-[14px] font-semibold text-ink">设备层级</h2>
      <AppIcon name="list-tree" :size="15" class="text-muted" />
    </div>

    <el-input
      v-model="keyword"
      class="ctl search-input mt-1"
      placeholder="搜索名称或编码"
      clearable
    >
      <template #suffix>
        <AppIcon name="search" :size="14" class="text-subtle" />
      </template>
    </el-input>

    <div class="my-1 h-px w-full bg-line" />

    <div class="min-h-0 flex-1 overflow-y-auto">
      <el-tree
        ref="treeRef"
        class="ems-tree"
        :data="device.tree"
        node-key="id"
        :props="{ label: 'name', children: 'children' }"
        :indent="13"
        :default-expanded-keys="device.expandedIds"
        :current-node-key="device.currentId"
        highlight-current
        :expand-on-click-node="false"
        :filter-node-method="filterNode"
        @node-click="onNodeClick"
      >
        <template #default="{ node, data }">
          <div class="flex w-full min-w-0 items-center gap-[7px]">
            <button
              v-if="hasChildren(data as DeviceNode)"
              type="button"
              class="flex shrink-0 items-center text-muted"
              @click.stop="node.expanded ? node.collapse() : node.expand()"
            >
              <AppIcon :name="node.expanded ? 'chevron-down' : 'chevron-right'" :size="12" />
            </button>
            <AppIcon v-else name="chevron-right" :size="12" class="shrink-0 text-subtle" />
            <AppIcon
              :name="(data as DeviceNode).icon"
              :size="13"
              :class="data.id === device.currentId ? 'text-accent' : 'text-muted'"
            />
            <span
              class="truncate text-[11px]"
              :class="data.id === device.currentId ? 'font-semibold text-accent' : 'text-ink'"
            >
              {{ data.name }}
            </span>
            <span v-if="(data as DeviceNode).count" class="ml-auto text-[10px] text-subtle">
              {{ (data as DeviceNode).count }}
            </span>
          </div>
        </template>
      </el-tree>
    </div>

    <div class="h-px w-full bg-line" />
    <p class="text-[10px] text-accent">当前范围：ess000001</p>
    <p class="text-[9px] text-muted">站 → 单元 → 设备 → 部件</p>
  </aside>
</template>
