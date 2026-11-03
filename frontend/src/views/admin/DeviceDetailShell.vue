<script setup lang="ts">
import { computed, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import AdminDeviceTree from '@/components/device/AdminDeviceTree.vue'
import DeviceToolbar from '@/components/admin/DeviceToolbar.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import { useDeviceStore } from '@/stores/device'
const device = useDeviceStore(),
  route = useRoute(),
  router = useRouter()
const id = computed(() => String(route.params.id))
const tabs = [
  { key: 'basic', label: '基本信息', path: 'basic' },
  { key: 'channels', label: '通道绑定', path: 'channels' },
  { key: 'points', label: '点表导入', path: 'points/import' },
  { key: 'mapping', label: '四遥点位', path: 'points/mapping' },
  { key: 'validate', label: '配置检查', path: 'release' },
]
const active = computed(() => tabs.find((t) => route.path.endsWith(t.path))?.key ?? 'basic')
watch(
  id,
  () => {
    void device.selectNode(id.value)
  },
  { immediate: true },
)
</script>
<template>
  <div class="device-workspace">
    <AdminDeviceTree />
    <section class="workspace-content">
      <DeviceToolbar
        ><span class="text-muted">{{ device.current?.path ?? id }}</span></DeviceToolbar
      >
      <section class="panel shrink-0 overflow-hidden">
        <div class="flex items-center gap-4 p-5">
          <h2 class="text-title font-semibold">{{ device.current?.name ?? id }}</h2>
          <span class="text-small text-muted"
            >{{ device.current?.code }} · {{ device.current?.device_type_code }}</span
          >
        </div>
        <UnderlineTabs
          :items="tabs"
          :model-value="active"
          @update:model-value="
            router.push(`/admin/devices/${id}/${tabs.find((t) => t.key === $event)?.path}`)
          "
        />
      </section>
      <AsyncContent :error="device.error" @retry="device.selectNode(id)">
        <router-view />
      </AsyncContent>
    </section>
  </div>
</template>
