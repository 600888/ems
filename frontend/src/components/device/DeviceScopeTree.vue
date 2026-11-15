<script setup lang="ts">
import { computed } from 'vue'
import AppIcon from '@/components/common/AppIcon.vue'
import type { DeviceNode } from '@/types/domain'

const props = withDefaults(
  defineProps<{
    tree: DeviceNode[]
    modelValue: string
    title?: string
    hint?: string
    counts?: Record<string, number>
    expandedKeys?: string[]
    width?: number
  }>(),
  { title: '设备目录', width: 288 },
)
const keyword = defineModel<string>('keyword', { default: '' })
const emit = defineEmits<{ select: [node: DeviceNode] }>()
const filteredTree = computed(() => {
  const term = keyword.value.trim().toLocaleLowerCase()
  const filter = (nodes: DeviceNode[]): DeviceNode[] =>
    nodes.flatMap((node) => {
      const children = filter(node.children ?? [])
      return !term ||
        `${node.name} ${node.code}`.toLocaleLowerCase().includes(term) ||
        children.length
        ? [{ ...node, children }]
        : []
    })
  return filter(props.tree)
})
</script>

<template>
  <aside
    class="flex shrink-0 flex-col gap-2 overflow-hidden border-r border-line bg-panel p-4"
    :style="{ width: `${width}px`, maxWidth: '100%' }"
  >
    <div class="flex items-center gap-2.5">
      <h2 class="text-lead font-semibold text-ink">{{ title }}</h2>
      <div class="flex-1" />
      <AppIcon name="list-tree" :size="16" class="text-muted" />
    </div>
    <el-input
      v-model="keyword"
      class="ctl search-input"
      placeholder="搜索设备名称 / 编码"
      clearable
      aria-label="搜索设备范围"
    >
      <template #prefix><AppIcon name="search" :size="14" class="text-subtle" /></template>
    </el-input>
    <slot name="toolbar" />
    <p v-if="hint" class="text-caption text-subtle">{{ hint }}</p>
    <div class="hairline my-1" />
    <div class="min-h-0 flex-1 overflow-y-auto">
      <el-tree
        class="ems-tree"
        :data="filteredTree"
        node-key="id"
        :props="{ label: 'name', children: 'children' }"
        :indent="13"
        :default-expanded-keys="
          expandedKeys ??
          tree.flatMap((node) => [
            node.id,
            ...(node.children ?? [])
              .filter((child) => child.kind === 'unit')
              .map((child) => child.id),
          ])
        "
        :current-node-key="modelValue"
        highlight-current
        :expand-on-click-node="false"
        @node-click="(node: DeviceNode) => emit('select', node)"
      >
        <template #default="{ node, data }">
          <div class="flex w-full min-w-0 items-center gap-1.5">
            <button
              v-if="data.children?.length"
              type="button"
              class="flex shrink-0 items-center text-muted"
              :aria-label="`${node.expanded ? '折叠' : '展开'}${data.name}`"
              @click.stop="node.expanded ? node.collapse() : node.expand()"
            >
              <AppIcon :name="node.expanded ? 'chevron-down' : 'chevron-right'" :size="12" />
            </button>
            <span v-else class="w-3 shrink-0" />
            <AppIcon
              :name="data.icon"
              :size="14"
              :class="data.id === modelValue ? 'text-accent' : 'text-muted'"
            />
            <span
              class="truncate text-small"
              :class="data.id === modelValue ? 'font-semibold text-ink' : 'text-muted'"
              :title="data.name"
              >{{ data.name }}</span
            >
            <div class="flex-1" />
            <span v-if="counts && counts[data.id]" class="text-small tabular-nums text-warn">{{
              counts[data.id]
            }}</span>
            <i
              v-else-if="!counts && data.status"
              class="dot"
              :class="
                data.status === 'alarm'
                  ? 'text-warn'
                  : data.status === 'online'
                    ? 'text-ok'
                    : 'text-subtle'
              "
            />
          </div>
        </template>
      </el-tree>
    </div>
    <div class="hairline" />
    <slot name="footer"><p class="text-caption text-subtle">站 → 单元 → 设备 → 部件</p></slot>
  </aside>
</template>
