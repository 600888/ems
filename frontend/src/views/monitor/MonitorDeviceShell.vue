<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'

import MetricStrip from '@/components/device/MetricStrip.vue'
import MonitorDeviceTree from '@/components/device/MonitorDeviceTree.vue'
import MonitorHeader from '@/components/device/MonitorHeader.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import { useMonitorStore } from '@/stores/monitor'
import { useAuthStore } from '@/stores/auth'
import type { TabItem } from '@/types/ui'

const route = useRoute()
const router = useRouter()
const monitor = useMonitorStore()
const auth = useAuthStore()

const tabs = computed<TabItem[]>(() => [
  { key: 'overview', label: '运行总览' },
  { key: 'telemetry', label: '遥测', count: monitor.profile.tabCounts.telemetry },
  { key: 'signals', label: '遥信', count: monitor.profile.tabCounts.signal },
  ...(auth.can('command.yk')
    ? [{ key: 'control', label: '遥控', count: monitor.profile.tabCounts.control }]
    : []),
  ...(auth.can('command.yt')
    ? [{ key: 'setpoints', label: '遥调', count: monitor.profile.tabCounts.setpoint }]
    : []),
  ...(auth.can('history.read') ? [{ key: 'curves', label: '历史曲线' }] : []),
])

const activeTab = computed(() => route.path.replace('/monitor/', '') || 'overview')

function changeTab(key: string) {
  router.push(`/monitor/${key}`)
}

let timer = 0
onMounted(() => {
  monitor.load()
  timer = window.setInterval(() => monitor.tick(), 2000)
})
onBeforeUnmount(() => window.clearInterval(timer))
</script>

<template>
  <div class="device-workspace flush-workspace">
    <MonitorDeviceTree />

    <section v-if="monitor.selectedId" class="workspace-content">
      <div v-if="monitor.error" class="flex shrink-0 items-center gap-3">
        <InfoBar class="flex-1" :text="monitor.error" tone="danger" /><ActionBtn
          @click="monitor.load"
          >重试</ActionBtn
        >
      </div>
      <MonitorHeader />
      <MetricStrip />
      <UnderlineTabs
        :model-value="activeTab"
        :items="tabs"
        :height="42"
        @update:model-value="changeTab"
      />
      <router-view />
    </section>
    <EmptyState
      v-else
      title="授权范围内暂无设备"
      :desc="monitor.error || '请联系管理员确认设备接入与授权范围。'"
      icon="inbox"
      class="flex-1"
      ><ActionBtn @click="monitor.load">重新加载</ActionBtn></EmptyState
    >
  </div>
</template>
