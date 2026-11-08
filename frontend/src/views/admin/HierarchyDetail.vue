<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import AdminDeviceTree from '@/components/device/AdminDeviceTree.vue'
import DeviceToolbar from '@/components/admin/DeviceToolbar.vue'
import BasicInfo from './BasicInfo.vue'
import PointMapping from './PointMapping.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import { deviceApi } from '@/api/device'
import { useDeviceStore } from '@/stores/device'
import { errorMessage } from '@/api/http'
import type { EngineeringRecord } from '@/types/engineering'
const route = useRoute(),
  device = useDeviceStore(),
  children = ref<EngineeringRecord[]>([]),
  tab = ref('basic'),
  error = ref(''),
  busy = ref(false)
const id = computed(() => String(route.params.id))
async function load() {
  busy.value = true
  error.value = ''
  try {
    await device.selectNode(id.value)
    children.value = (await deviceApi.list({ parent_device_id: id.value })).items
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
watch(id, load, { immediate: true })
</script>
<template>
  <div class="device-workspace">
    <AdminDeviceTree />
    <section class="workspace-content">
      <DeviceToolbar>
        <h2 class="text-title font-semibold">{{ device.current?.name }}</h2>
        <ActionBtn @click="tab = 'basic'">基本信息</ActionBtn
        ><ActionBtn @click="tab = 'children'">子节点</ActionBtn
        ><ActionBtn v-if="device.current?.node_kind === 'component'" @click="tab = 'points'"
          >四遥点位</ActionBtn
        >
      </DeviceToolbar>
      <BasicInfo v-if="tab === 'basic'" /><PointMapping v-else-if="tab === 'points'" /><AsyncContent
        v-else
        :loading="busy"
        :error="error"
        :empty="!children.length"
        empty-title="暂无子节点"
        @retry="load"
        ><div class="panel overflow-x-auto">
          <table class="identity-table">
            <thead>
              <tr>
                <th>名称</th>
                <th>编码</th>
                <th>种类</th>
                <th>设备类型</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="child in children" :key="child.device_id">
                <td>{{ child.name }}</td>
                <td>{{ child.code }}</td>
                <td>{{ child.node_kind }}</td>
                <td>{{ child.device_type_code || '—' }}</td>
              </tr>
            </tbody>
          </table>
        </div></AsyncContent
      >
    </section>
  </div>
</template>
