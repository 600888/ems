<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import AdminDeviceTree from '@/components/device/AdminDeviceTree.vue'
import { useDeviceStore } from '@/stores/device'

const route = useRoute()
const router = useRouter()
const device = useDeviceStore()

const deviceId = computed(() => String(route.params.id))
watch(
  deviceId,
  (id) => {
    device.selectNode(id)
    if (!device.rows.length) device.load()
  },
  { immediate: true },
)

const row = computed(() => device.rows.find((item) => item.id === deviceId.value) ?? device.rows[0])

const tabs = [
  { key: 'basic', label: '基本信息', path: 'basic' },
  { key: 'diagnose', label: '点位诊断', path: '' },
  { key: 'channels', label: '通道绑定', path: 'channels' },
  { key: 'points', label: '厂家点表', path: 'points/import' },
  { key: 'builtin', label: '固有点位', path: 'points/mapping' },
  { key: 'release', label: '配置版本', path: 'release' },
]

const activeTab = computed(() => {
  const sub = route.path.replace(`/admin/devices/${deviceId.value}/`, '')
  if (sub.startsWith('points/import')) return 'points'
  if (sub.startsWith('points/mapping')) return 'builtin'
  return ['basic', 'channels', 'release'].includes(sub) ? sub : 'basic'
})

const current = ref('basic')
watch(activeTab, (value) => (current.value = value), { immediate: true })

function goTab(key: string) {
  const target = tabs.find((item) => item.key === key)
  if (!target?.path) {
    ElMessage.info('「点位诊断」不在本次示例范围内')
    return
  }
  router.push(`/admin/devices/${deviceId.value}/${target.path}`)
}
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
        <span class="text-[11px] text-accent">{{ row?.name ?? '设备' }}</span>
      </div>

      <!-- 设备身份与分栏导航 -->
      <section class="panel flex shrink-0 flex-col overflow-hidden">
        <div class="flex items-center gap-[14px] px-5 py-[18px]">
          <div class="flex h-12 w-12 items-center justify-center rounded-[7px] bg-iconbox">
            <AppIcon
              :name="row?.type === 'BMS' ? 'battery-charging' : 'cpu'"
              :size="22"
              class="text-accent"
            />
          </div>

          <div class="flex min-w-0 flex-1 flex-col gap-[6px]">
            <div class="flex items-center gap-3">
              <h2 class="truncate text-[20px] leading-none font-semibold text-ink">
                {{ row?.name }}
              </h2>
              <StatusTag tone="accent" label="已启用" />
            </div>
            <p class="truncate text-[11px] text-muted">
              {{ row?.code }} / {{ row?.type }} / {{ row?.vendor }} / 当前生效 v12
            </p>
          </div>

          <button
            type="button"
            class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink transition-colors hover:border-subtle"
            @click="router.push(`/admin/devices/${deviceId}/points/import`)"
          >
            <AppIcon name="upload" :size="15" />
            导入点表
          </button>
          <button
            type="button"
            class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink transition-colors hover:border-subtle"
            @click="ElMessage.info('设备档案编辑不在本次示例范围内')"
          >
            <AppIcon name="pencil" :size="15" />
            编辑设备
          </button>
        </div>

        <UnderlineTabs
          :model-value="current"
          :items="tabs"
          :height="44"
          @update:model-value="goTab"
        />
      </section>

      <router-view class="min-h-0 flex-1" />
    </section>
  </div>
</template>
