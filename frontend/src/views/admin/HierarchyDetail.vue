<script setup lang="ts">
import {computed, ref} from 'vue'
import {ElMessage} from 'element-plus'
import {useRoute} from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import AdminDeviceTree from '@/components/device/AdminDeviceTree.vue'
import {subNodes} from '@/mocks/devices'
import {clusterMetrics} from '@/mocks/points'
import type {TabItem} from '@/types/ui'

const route = useRoute()

const nodeId = computed(() => String(route.params.id))
const nodeName = computed(() => {
  if (nodeId.value.startsWith('rack')) return 'Rack 01 · 电池簇'
  if (nodeId.value.startsWith('pack')) return 'Pack 01 · 电池包'
  return '电池簇'
})

const tabs: TabItem[] = [
  {key: 'overview', label: '节点概况'},
  {key: 'children', label: '子节点'},
  {key: 'diagnose', label: '点位诊断'},
  {key: 'source', label: '数据来源'},
]
const tab = ref('overview')

function changeTab(key: string) {
  if (key === 'overview') {
    tab.value = key
    return
  }
  ElMessage.info(`「${tabs.find((item) => item.key === key)?.label}」不在本次示例范围内`)
}

const sourceChain = ['BMS · bms01', 'battery01', 'rack01', 'pack / cell 固有点位']
</script>

<template>
  <div class="flex min-h-0 flex-1 gap-5">
    <AdminDeviceTree />

    <section class="flex min-w-0 flex-1 flex-col gap-[18px]">
      <!-- 当前设备路径 -->
      <div class="flex h-[22px] shrink-0 items-center gap-[7px]">
        <span class="text-[11px] text-muted">华东示范储能站</span>
        <AppIcon name="chevron-right" :size="12" class="text-subtle" />
        <span class="text-[11px] text-muted">储能单元 01</span>
        <AppIcon name="chevron-right" :size="12" class="text-subtle" />
        <span class="text-[11px] text-accent">battery01 / rack01</span>
      </div>

      <!-- 电池簇身份 -->
      <section class="panel flex shrink-0 flex-col overflow-hidden">
        <div class="flex items-center gap-[14px] px-5 py-[18px]">
          <div class="flex h-12 w-12 items-center justify-center rounded-[7px] bg-iconbox">
            <AppIcon name="battery" :size="22" class="text-accent" />
          </div>
          <div class="flex min-w-0 flex-1 flex-col gap-[6px]">
            <div class="flex items-center gap-3">
              <h2 class="truncate text-[20px] leading-none font-semibold text-ink">{{ nodeName }}</h2>
              <StatusTag tone="mute" label="层级节点" :dot="false" />
            </div>
            <p class="truncate text-[11px] text-muted">
              ess000001 / battery01 / rack01 · 8 Pack / 416 Cell
            </p>
          </div>
          <button
            type="button"
            class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink"
            @click="ElMessage.info('节点编辑不在本次示例范围内')"
          >
            <AppIcon name="pencil" :size="15" />
            编辑节点
          </button>
        </div>
        <UnderlineTabs :model-value="tab" :items="tabs" :height="44" @update:model-value="changeTab" />
      </section>

      <!-- 电池簇指标 -->
      <div class="flex h-[88px] shrink-0 gap-3">
        <article
          v-for="item in clusterMetrics"
          :key="item.label"
          class="flex min-w-0 flex-1 flex-col justify-center gap-[5px] rounded-[6px] border border-line bg-panel px-4 py-3"
        >
          <span class="text-[11px] text-muted">{{ item.label }}</span>
          <div class="flex items-baseline gap-[6px]">
            <span
              class="text-[25px] leading-none font-semibold"
              :class="item.tone === 'accent' ? 'text-accent' : 'text-ink'"
            >
              {{ item.value }}
            </span>
            <span class="text-[11px] text-muted">{{ item.unit }}</span>
          </div>
          <p class="truncate text-[10px] text-subtle">{{ item.note }}</p>
        </article>
      </div>

      <!-- 电池包子节点 -->
      <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">子节点列表</h3>
          <span class="text-[11px] text-muted">8 个节点 · 选择节点查看下一级</span>
        </div>

        <div class="dt-head shrink-0">
          <div class="min-w-0 flex-1"><span class="dt-th">节点名称 / code</span></div>
          <div class="w-[130px] shrink-0"><span class="dt-th">电芯数量</span></div>
          <div class="w-[150px] shrink-0"><span class="dt-th">映射点位</span></div>
          <div class="w-[180px] shrink-0"><span class="dt-th">点表模板</span></div>
          <div class="w-[150px] shrink-0"><span class="dt-th">配置状态</span></div>
          <div class="w-[220px] shrink-0"><span class="dt-th">采集来源</span></div>
          <div class="w-[234px] shrink-0"><span class="dt-th">操作</span></div>
        </div>

        <div class="min-h-0 flex-1 overflow-y-auto">
          <div v-for="row in subNodes" :key="row.path" class="dt-row h-[52px]">
            <div class="dt-cell flex min-w-0 flex-1 flex-col gap-[3px]">
              <span class="text-[11px] font-semibold text-ink">{{ row.name }}</span>
              <span class="truncate font-mono text-[9px] text-subtle">{{ row.path }}</span>
            </div>
            <div class="dt-cell w-[130px] shrink-0 text-[11px] text-ink">{{ row.cells }}</div>
            <div class="dt-cell w-[150px] shrink-0 text-[11px] text-ink">{{ row.mapped }}</div>
            <div class="dt-cell w-[180px] shrink-0 text-[11px] text-muted">{{ row.template }}</div>
            <div class="dt-cell w-[150px] shrink-0">
              <StatusTag
                :tone="row.versionState === 'draft' ? 'warn' : 'ok'"
                :label="row.versionState === 'draft' ? '有草稿' : '已生效'"
              />
            </div>
            <div class="dt-cell w-[220px] shrink-0 truncate text-[10px] text-muted">{{ row.source }}</div>
            <div class="dt-cell w-[234px] shrink-0">
              <span class="link-action" @click="ElMessage.info('子节点详情不在本次示例范围内')">查看 · 点位</span>
            </div>
          </div>
        </div>

        <div class="flex h-[46px] shrink-0 items-center justify-between px-4">
          <span class="text-[10px] text-muted">显示 1—4 项，共 8 项</span>
          <el-pagination class="ems-pager" small background layout="prev, pager, next" :total="8" :page-size="4" />
        </div>
      </section>

      <!-- 来源关系 -->
      <section class="panel flex shrink-0 flex-col gap-3 p-4">
        <div class="flex items-center justify-between">
          <h3 class="text-[14px] font-semibold text-ink">数据来源</h3>
          <span class="text-[11px] text-muted">映射配置 · 静态</span>
        </div>
        <div class="flex items-center gap-2">
          <template v-for="(item, index) in sourceChain" :key="item">
            <span class="rounded-[4px] border border-line bg-raised px-2 py-1 text-[11px] text-ink">{{ item }}</span>
            <AppIcon v-if="index < sourceChain.length - 1" name="arrow-right" :size="13" class="text-subtle" />
          </template>
        </div>
        <p class="text-[10px] text-muted">
          采集绑定与厂家点表维护于 bms01；本节点无需独立通信连接，映射保留来源设备与目标节点。
        </p>
      </section>
    </section>
  </div>
</template>
