<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'

import MetricStrip from '@/components/device/MetricStrip.vue'
import MonitorDeviceTree from '@/components/device/MonitorDeviceTree.vue'
import MonitorHeader from '@/components/device/MonitorHeader.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import { useMonitorStore } from '@/stores/monitor'
import type { TabItem } from '@/types/ui'

const route = useRoute()
const router = useRouter()
const monitor = useMonitorStore()

const tabs = computed<TabItem[]>(() => [
  { key: 'overview', label: '运行总览' },
  { key: 'telemetry', label: '遥测', count: monitor.profile.tabCounts.telemetry },
  { key: 'signals', label: '遥信', count: monitor.profile.tabCounts.signal },
  { key: 'control', label: '遥控', count: monitor.profile.tabCounts.control },
  { key: 'setpoints', label: '遥调', count: monitor.profile.tabCounts.setpoint },
  { key: 'curves', label: '实时曲线' },
])

const activeTab = computed(() => route.path.replace('/monitor/', '') || 'overview')

/** 设计图中遥控 / 遥调画板以 PCS 01 为例，切页签时同步切到具备该能力的设备 */
const tabDevice: Record<string, string> = { control: 'pcs01', setpoints: 'pcs01' }

function changeTab(key: string) {
  const target = tabDevice[key]
  if (target && monitor.selectedId !== target) monitor.selectDevice(target)
  router.push(`/monitor/${key}`)
}

let timer = 0
onMounted(() => {
  const initial = tabDevice[activeTab.value]
  if (initial) monitor.selectDevice(initial)
  monitor.load()
  timer = window.setInterval(() => monitor.tick(), 2000)
})
onBeforeUnmount(() => window.clearInterval(timer))
</script>

<template>
  <div class="flex min-h-0 flex-1 gap-5">
    <MonitorDeviceTree />

    <section class="flex min-w-0 flex-1 flex-col gap-4">
      <MonitorHeader />
      <MetricStrip />
      <UnderlineTabs
        :model-value="activeTab"
        :items="tabs"
        :height="42"
        @update:model-value="changeTab"
      />
      <router-view class="min-h-0 flex-1" />
    </section>
  </div>
</template>
