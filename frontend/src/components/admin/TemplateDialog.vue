<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { deviceApi, engineeringRevision } from '@/api/device'
import { errorMessage } from '@/api/http'
import { useDeviceStore } from '@/stores/device'
import ActionBtn from '@/components/common/ActionBtn.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import type { Binding } from '@/types/engineering'
const open = defineModel<boolean>({ default: false }),
  device = useDeviceStore(),
  rows = ref<Record<string, unknown>[]>([]),
  bindings = ref<Binding[]>([]),
  binding = ref(''),
  selected = ref(''),
  busy = ref(false),
  error = ref('')
const template = computed(() =>
  rows.value.find((r) => `${r.template_id}@${r.template_version}` === selected.value),
)
let expected = ''
watch(open, async (value) => {
  if (!value) return
  busy.value = true
  error.value = ''
  try {
    rows.value = (await deviceApi.templates()).items
    bindings.value =
      device.current?.node_kind === 'device'
        ? (await deviceApi.bindings(device.currentId)).items
        : []
    binding.value = bindings.value[0]?.binding_id ?? ''
    expected = engineeringRevision()
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
})
async function apply() {
  if (!template.value || !binding.value) return
  busy.value = true
  error.value = ''
  try {
    await deviceApi.applyTemplate(
      {
        device_id: device.currentId,
        binding_id: binding.value,
        template_id: template.value.template_id,
        template_version: template.value.template_version,
        mode: 'merge_missing',
      },
      expected,
    )
    ElMessage.success('模板已应用到当前配置')
    open.value = false
    await device.load()
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
</script>
<template>
  <el-dialog v-model="open" title="型号模板" width="760px"
    ><div class="flex flex-col gap-4">
      <InfoBar v-if="error" :text="error" tone="danger" />
      <p class="text-[13px] text-muted">
        选择设备详情中的采集绑定后，可以补充应用型号模板中尚未配置的点位。
      </p>
      <el-select v-model="selected" placeholder="选择模板"
        ><el-option
          v-for="row in rows"
          :key="`${row.template_id}@${row.template_version}`"
          :label="`${row.name ?? row.template_id} / ${row.template_version}`"
          :value="`${row.template_id}@${row.template_version}`" /></el-select
      ><el-select v-model="binding" placeholder="当前设备的采集绑定"
        ><el-option
          v-for="b in bindings"
          :key="b.binding_id"
          :label="`${b.channel?.name} / ${b.device_address}`"
          :value="b.binding_id"
      /></el-select>
      <p v-if="template" class="text-[13px] text-muted">
        {{ template.vendor }} {{ template.model }} · {{ template.device_type_code }}
      </p>
      <p v-if="!rows.length && !busy" class="text-muted">暂无可用型号模板</p>
    </div>
    <template #footer
      ><ActionBtn variant="primary" :disabled="!template || !binding" :loading="busy" @click="apply"
        >补充应用到当前设备</ActionBtn
      ></template
    ></el-dialog
  >
</template>
