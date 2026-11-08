<script setup lang="ts">
import { computed } from 'vue'

import AppIcon from '@/components/common/AppIcon.vue'
import { errorMessage } from '@/api/http'
import {
  useHistoryStore,
  type HistoryDeviceNode,
  type HistoryPointNode,
  type HistoryTreeChild,
} from '@/stores/history'

const history = useHistoryStore()
const tree = computed(() => history.filteredTree)

function isPoint(data: HistoryTreeChild): data is HistoryPointNode {
  return data.isPoint === true
}

function checkIcon(data: HistoryTreeChild) {
  if (isPoint(data)) return history.isSelected(data.pointId) ? 'square-check' : 'square'
  return history.deviceCheckState(data) === 'all' ? 'square-check' : 'square'
}

function checkClass(data: HistoryTreeChild) {
  return checkIcon(data) === 'square-check' ? 'text-accent' : 'text-subtle'
}

function hasChildren(data: HistoryTreeChild) {
  if (isPoint(data)) return false
  return true
}

/** 展开设备节点前懒加载测点子节点 */
async function onExpand(
  node: { expanded: boolean; collapse: () => void; expand: () => void },
  data: HistoryTreeChild,
) {
  if (node.expanded) {
    node.collapse()
    return
  }
  if (isPoint(data)) return
  try {
    await history.ensurePoints(data as HistoryDeviceNode)
    node.expand()
  } catch (error) {
    history.error = errorMessage(error)
  }
}

function onCheck(data: HistoryTreeChild) {
  if (isPoint(data)) history.togglePoint(data)
  else void history.toggleDevice(data as HistoryDeviceNode)
}

function onNodeClick(data: unknown, node: unknown) {
  const d = data as HistoryTreeChild
  if (isPoint(d)) return
  void onExpand(node as { expanded: boolean; collapse: () => void; expand: () => void }, d)
}

async function loadExpanded(data: HistoryTreeChild) {
  if (isPoint(data)) return
  try {
    await history.ensurePoints(data)
  } catch (error) {
    history.error = errorMessage(error)
  }
}

function dotClass(status?: string) {
  if (status === 'alarm') return 'text-warn'
  if (status === 'offline') return 'text-subtle'
  if (status === 'online') return 'text-ok'
  return 'text-subtle'
}
</script>

<template>
  <aside
    class="flex w-[288px] shrink-0 flex-col gap-1 overflow-hidden border-r border-line bg-panel p-4"
  >
    <div class="flex items-center gap-[10px]">
      <h2 class="text-lead font-semibold text-ink">设备目录</h2>
      <div class="flex-1" />
      <AppIcon name="list-filter" :size="15" class="text-muted" />
    </div>

    <el-input
      v-model="history.keyword"
      class="ctl search-input mt-1"
      placeholder="搜索设备 / 已加载测点"
      clearable
    >
      <template #prefix>
        <AppIcon name="search" :size="14" class="text-subtle" />
      </template>
    </el-input>

    <div class="my-1 h-px w-full bg-line" />

    <div class="min-h-0 flex-1 overflow-y-auto pl-1">
      <p v-if="history.loading" class="py-4 text-caption text-muted">正在加载设备目录…</p>
      <p v-else-if="!tree.length" class="py-4 text-caption text-muted">暂无匹配的设备或测点</p>
      <el-tree
        class="ems-tree"
        :data="tree"
        node-key="id"
        :props="{ label: 'name', children: 'children' }"
        :indent="13"
        highlight-current
        :expand-on-click-node="false"
        @node-click="onNodeClick"
        @node-expand="loadExpanded"
      >
        <template #default="{ node, data }">
          <div class="flex w-full min-w-0 items-center gap-[6px]">
            <button
              v-if="hasChildren(data as HistoryTreeChild)"
              type="button"
              class="flex shrink-0 items-center text-muted"
              @click.stop="onExpand(node, data as HistoryTreeChild)"
            >
              <AppIcon :name="node.expanded ? 'chevron-down' : 'chevron-right'" :size="12" />
            </button>
            <span v-else class="w-3 shrink-0" />
            <button
              v-if="
                isPoint(data as HistoryTreeChild) || ['device', 'component'].includes(data.kind)
              "
              type="button"
              class="flex shrink-0 items-center"
              :class="checkClass(data as HistoryTreeChild)"
              :aria-label="
                checkIcon(data as HistoryTreeChild) === 'square-check' ? '取消勾选' : '勾选'
              "
              @click.stop="onCheck(data as HistoryTreeChild)"
            >
              <AppIcon :name="checkIcon(data as HistoryTreeChild)" :size="14" />
            </button>
            <AppIcon
              :name="(data as HistoryTreeChild).icon"
              :size="13"
              :class="
                history.isSelected((data as HistoryPointNode).pointId ?? '')
                  ? 'text-accent'
                  : 'text-muted'
              "
            />
            <span
              class="truncate text-small"
              :class="
                isPoint(data as HistoryTreeChild) &&
                history.isSelected((data as HistoryPointNode).pointId)
                  ? 'font-semibold text-ink'
                  : 'text-muted'
              "
            >
              {{ data.name }}
            </span>
            <div class="flex-1" />
            <i
              v-if="!isPoint(data as HistoryTreeChild) && (data as HistoryDeviceNode).status"
              class="dot"
              :class="dotClass((data as HistoryDeviceNode).status)"
            />
          </div>
        </template>
      </el-tree>
    </div>

    <div class="h-px w-full bg-line" />

    <div class="flex items-center gap-3 py-[2px]">
      <span class="flex items-center gap-[5px] text-caption text-muted">
        <i class="dot text-ok" />在线
      </span>
      <span class="flex items-center gap-[5px] text-caption text-muted">
        <i class="dot text-warn" />告警
      </span>
      <span class="flex items-center gap-[5px] text-caption text-muted">
        <i class="dot text-subtle" />离线
      </span>
    </div>
    <p class="text-caption text-subtle">勾选设备或测点加入对比</p>
  </aside>
</template>
