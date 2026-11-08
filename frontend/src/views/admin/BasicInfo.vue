<script setup lang="ts">
import { ref, reactive, watch } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { deviceApi, engineeringRevision, revisionOf, warningsOf } from '@/api/device'
import type { Finding } from '@/types/engineering'
import { errorMessage } from '@/api/http'
import { useDeviceStore } from '@/stores/device'
const route = useRoute(),
  device = useDeviceStore(),
  busy = ref(false),
  error = ref('')
const warnings = ref<Finding[]>([])
const form = reactive({
  name: '',
  vendor: '',
  model: '',
  firmware_version: '',
  description: '',
  enabled: 0,
})
async function load() {
  busy.value = true
  error.value = ''
  warnings.value = []
  try {
    const row = await deviceApi.detail(String(route.params.id))
    Object.assign(
      form,
      Object.fromEntries(
        Object.keys(form).map((k) => [k, k === 'enabled' ? Number(row[k]) : row[k]]),
      ),
    )
    editRevision = revisionOf(row)
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function save() {
  busy.value = true
  error.value = ''
  try {
    const saved = await deviceApi.update(
      String(route.params.id),
      device.current?.node_kind === 'station' ? { name: form.name } : { ...form },
      editRevision,
    )
    warnings.value = warningsOf(saved)
    await device.selectNode(String(route.params.id))
    editRevision = engineeringRevision()
    ElMessage.success('设备资料已保存并生效')
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
watch(() => route.params.id, load, { immediate: true })

let editRevision = ''
</script>
<template>
  <AsyncContent :error="error" @retry="load"
    ><form class="panel flex flex-col gap-6 p-6" @submit.prevent="save">
      <InfoBar
        v-if="warnings.length"
        tone="warn"
        :text="`设备资料已保存并生效，存在 ${warnings.length} 项配置提示：${[...new Set(warnings.map((item) => item.message || item.reason))].join('；')}`"
      />
      <div class="grid gap-6 md:grid-cols-2">
        <FormField
          v-for="field in [
            { key: 'name', label: '设备名称' },
            { key: 'vendor', label: '厂家' },
            { key: 'model', label: '型号' },
            { key: 'firmware_version', label: '固件版本' },
          ] as const"
          :key="field.key"
          :label="field.label"
          v-show="device.current?.node_kind !== 'station' || field.key === 'name'"
          v-slot="slot"
          ><el-input :id="slot.id" v-model="form[field.key]" :disabled="busy"
        /></FormField>
      </div>
      <FormField v-if="device.current?.node_kind !== 'station'" label="描述" v-slot="slot"
        ><el-input :id="slot.id" v-model="form.description" type="textarea" /></FormField
      ><label
        v-if="device.current?.node_kind !== 'station'"
        class="flex items-center gap-3 text-body"
        >启用设备<el-switch v-model="form.enabled" :active-value="1" :inactive-value="0"
      /></label>
      <div class="flex justify-end">
        <ActionBtn type="submit" variant="primary" :loading="busy">保存</ActionBtn>
      </div>
    </form></AsyncContent
  >
</template>
