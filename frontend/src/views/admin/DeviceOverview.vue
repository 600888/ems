<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import AdminDeviceTree from '@/components/device/AdminDeviceTree.vue'
import { useDeviceStore } from '@/stores/device'

const router = useRouter()
const route = useRoute()
const device = useDeviceStore()

onMounted(() => {
  if (!device.rows.length) device.load()
  // 画板 07：支持 /admin/devices?new=1 直接展开新建设备抽屉
  if (route.query.new) device.createDrawerOpen = true
})

const tabs = [
  { key: 'list', label: '设备与部件  12' },
  { key: 'tree', label: '设备层级' },
  { key: 'log', label: '配置记录' },
]
const activeTab = ref('list')

const stats = [
  {
    label: '直属节点',
    value: '12',
    unit: '项',
    note: '当前层级 · 设备与部件',
    icon: 'hard-drive',
    tone: 'text-accent',
  },
  {
    label: '固有点位映射',
    value: '268',
    unit: '项',
    note: '共 288 项 · 覆盖率 93.1%',
    icon: 'list-checks',
    tone: 'text-accent',
  },
  {
    label: '待补充点位',
    value: '20',
    unit: '项',
    note: '2 台设备存在缺失点位',
    icon: 'unlink',
    tone: 'text-warn',
  },
  {
    label: '待发布配置',
    value: '02',
    unit: '项',
    note: '草稿尚未影响运行',
    icon: 'file-clock',
    tone: 'text-accent',
  },
]

const allChecked = computed(
  () => device.paged.length > 0 && device.selectedIds.length === device.paged.length,
)

function openRow(id: string, action: string) {
  if (action === '层级') {
    router.push(`/admin/hierarchy/${id}`)
    return
  }
  if (action === '配置') {
    router.push(`/admin/devices/${id}/channels`)
    return
  }
  router.push(`/admin/devices/${id}/basic`)
}
</script>

<template>
  <div class="flex min-h-0 flex-1 gap-5">
    <AdminDeviceTree />

    <section class="flex min-w-0 flex-1 flex-col gap-[18px]">
      <!-- 面包屑 -->
      <div class="flex h-[22px] shrink-0 items-center gap-2">
        <AppIcon name="building-2" :size="13" class="text-muted" />
        <span class="text-[11px] text-muted">华东示范储能站</span>
        <AppIcon name="chevron-right" :size="12" class="text-subtle" />
        <span class="text-[11px] text-accent">储能单元 01</span>
      </div>

      <!-- 单元设备统计 -->
      <div class="flex h-[88px] shrink-0 gap-3">
        <article
          v-for="item in stats"
          :key="item.label"
          class="flex min-w-0 flex-1 flex-col gap-1 rounded-[6px] border border-line bg-panel px-4 py-3"
        >
          <div class="flex items-center justify-between">
            <span class="text-[11px] text-muted">{{ item.label }}</span>
            <AppIcon :name="item.icon" :size="13" :class="item.tone" />
          </div>
          <div class="flex items-baseline gap-[7px]">
            <span class="text-[26px] leading-none font-semibold" :class="item.tone">{{
              item.value
            }}</span>
            <span class="text-[10px] text-muted">{{ item.unit }}</span>
          </div>
          <p class="truncate text-[9px] text-subtle">{{ item.note }}</p>
        </article>
      </div>

      <!-- 设备列表面板 -->
      <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
        <UnderlineTabs v-model="activeTab" :items="tabs" :height="44" />

        <!-- 列表筛选 -->
        <div class="flex h-14 shrink-0 items-center gap-[10px] px-4">
          <el-input
            class="ctl search-input w-[232px]!"
            placeholder="搜索设备名称 / 编码"
            :model-value="device.keyword"
            clearable
            @update:model-value="device.setKeyword($event)"
          >
            <template #suffix>
              <AppIcon name="search" :size="14" class="text-subtle" />
            </template>
          </el-input>

          <el-select
            class="ctl w-[126px]!"
            :model-value="device.typeFilter"
            @update:model-value="device.setType($event)"
          >
            <el-option v-for="item in device.typeOptions" :key="item" :label="item" :value="item" />
          </el-select>

          <el-select
            class="ctl w-[126px]!"
            :model-value="device.configFilter"
            @update:model-value="device.setConfigState($event)"
          >
            <el-option
              v-for="item in device.configOptions"
              :key="item"
              :label="item"
              :value="item"
            />
          </el-select>

          <div class="flex-1" />

          <button
            type="button"
            class="flex h-[38px] shrink-0 items-center gap-[7px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium whitespace-nowrap text-ink transition-colors hover:border-subtle"
          >
            <AppIcon name="download" :size="15" />
            导出
          </button>
          <button
            type="button"
            class="flex h-[38px] shrink-0 items-center gap-[7px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium whitespace-nowrap text-ink transition-colors hover:border-subtle"
            @click="device.load()"
          >
            <AppIcon name="refresh-cw" :size="15" />
            刷新
          </button>
        </div>

        <!-- 表头 -->
        <div class="dt-head shrink-0">
          <div class="w-7 shrink-0">
            <el-checkbox :model-value="allChecked" size="small" @change="device.toggleAll()" />
          </div>
          <div class="min-w-0 flex-1"><span class="dt-th">设备名称</span></div>
          <div class="w-[140px] shrink-0"><span class="dt-th">设备类型</span></div>
          <div class="w-[180px] shrink-0"><span class="dt-th">设备编码</span></div>
          <div class="w-[190px] shrink-0"><span class="dt-th">固有点位</span></div>
          <div class="w-[190px] shrink-0"><span class="dt-th">配置版本</span></div>
          <div class="w-[194px] shrink-0"><span class="dt-th">操作</span></div>
        </div>

        <!-- 数据 -->
        <div class="min-h-0 flex-1 overflow-y-auto">
          <template v-if="device.loading">
            <div class="flex flex-col gap-2 p-4">
              <el-skeleton v-for="i in 5" :key="i" :rows="1" animated />
            </div>
          </template>

          <EmptyState
            v-else-if="device.error"
            icon="cloud-alert"
            title="加载失败"
            :desc="device.error"
            :min-height="194"
            class="m-4"
          >
            <button
              type="button"
              class="mt-1 h-[34px] rounded-[5px] border border-line bg-raised px-[14px] text-[12px] font-medium text-ink"
              @click="device.load()"
            >
              重试
            </button>
          </EmptyState>

          <template v-else-if="device.paged.length">
            <div
              v-for="row in device.paged"
              :key="row.id"
              class="dt-row h-[52px] shrink-0 transition-colors"
              :class="device.selectedIds.includes(row.id) && 'dt-row-selected'"
            >
              <div class="w-7 shrink-0">
                <el-checkbox
                  :model-value="device.selectedIds.includes(row.id)"
                  size="small"
                  @change="device.toggleRow(row.id)"
                />
              </div>
              <div class="dt-cell flex min-w-0 flex-1 flex-col gap-[3px]">
                <span class="truncate text-[11px] font-semibold text-ink">{{ row.name }}</span>
                <span class="truncate text-[9px] text-subtle">{{ row.vendor }}</span>
              </div>
              <div class="dt-cell w-[140px] shrink-0 text-[11px] text-ink">{{ row.type }}</div>
              <div class="dt-cell w-[180px] shrink-0 text-[11px] text-ink">{{ row.code }}</div>
              <div class="dt-cell w-[190px] shrink-0 text-[11px] text-ink">{{ row.points }}</div>
              <div
                class="dt-cell w-[190px] shrink-0 text-[11px]"
                :class="row.versionState === 'draft' ? 'text-warn' : 'text-ink'"
              >
                {{ row.version }}
              </div>
              <div
                class="dt-cell flex w-[194px] shrink-0 items-center gap-2 text-[11px] text-accent"
              >
                <template v-for="(action, index) in row.actions" :key="action">
                  <span class="cursor-pointer hover:underline" @click="openRow(row.id, action)">{{
                    action
                  }}</span>
                  <span v-if="index < row.actions.length - 1" class="text-subtle">·</span>
                </template>
              </div>
            </div>
          </template>

          <EmptyState
            v-else
            icon="hard-drive"
            title="暂无设备"
            desc="当前层级下还没有设备，创建后配置接入。"
            :min-height="194"
            class="m-4"
          />
        </div>

        <!-- 分页 -->
        <div class="flex h-[46px] shrink-0 items-center justify-between px-4">
          <span class="text-[10px] text-muted">
            共 {{ device.total }} 项设备与部件 · 已选 {{ device.selectedIds.length }} 项
          </span>
          <div class="flex items-center gap-2">
            <span class="text-[10px] text-muted">{{ device.pageSize }} 条 / 页</span>
            <el-pagination
              class="ems-pager"
              small
              background
              layout="prev, pager, next"
              :page-size="device.pageSize"
              :total="device.filtered.length"
              :current-page="device.page"
              @current-change="device.page = $event"
            />
          </div>
        </div>
      </section>

      <!-- 配置与校验摘要 -->
      <div class="flex h-[112px] shrink-0 gap-[14px]">
        <article class="panel flex min-w-0 flex-1 flex-col gap-3 px-4 py-4">
          <div class="flex items-center justify-between">
            <h3 class="text-[12px] font-semibold text-ink">配置应用状态</h3>
            <span
              class="link-action text-[10px]"
              @click="router.push('/admin/devices/pcs01/release')"
              >查看版本 →</span
            >
          </div>
          <div class="flex justify-between">
            <div
              v-for="stage in ['已发布', '采集已应用', '策略已应用']"
              :key="stage"
              class="flex flex-col gap-[6px]"
            >
              <span class="text-[10px] text-muted">{{ stage }}</span>
              <span class="text-[13px] font-medium text-ok">✓ v12</span>
            </div>
          </div>
        </article>

        <article class="panel flex min-w-0 flex-1 flex-col gap-[10px] px-4 py-4">
          <div class="flex items-center justify-between">
            <div class="flex items-center gap-[7px]">
              <AppIcon name="clipboard-list" :size="14" class="text-warn" />
              <span class="text-[12px] font-medium text-warn">2 台设备配置待补充</span>
            </div>
            <span class="link-action text-[10px]">查看 →</span>
          </div>
          <p class="text-[10px] text-muted">hvac01 温控点位缺失 2 项</p>
          <p class="text-[10px] text-muted">env01 未绑定点表模板，需补充映射</p>
        </article>
      </div>
    </section>
  </div>
</template>
