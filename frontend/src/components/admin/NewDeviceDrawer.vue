<script setup lang="ts">
import { reactive, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { deviceApi, stableId, engineeringRevision } from '@/api/device'
import { errorMessage } from '@/api/http'
import { useDeviceStore } from '@/stores/device'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import type { EngineeringRecord } from '@/types/engineering'
const device = useDeviceStore(),
  router = useRouter(),
  busy = ref(false),
  error = ref(''),
  types = ref<Record<string, unknown>[]>([]),
  parents = ref<EngineeringRecord[]>([])
const form = reactive({
  name: '',
  code: '',
  node_kind: 'device',
  parent_device_id: '',
  source_device_id: '',
  type: '',
  vendor: '',
  model: '',
  firmware_version: '',
  description: '',
})
let id = '',
  editRevision = ''
watch(
  () => device.createDrawerOpen,
  async (open) => {
    if (!open) return
    id = stableId('dev')
    error.value = ''
    Object.assign(form, {
      name: '',
      code: '',
      parent_device_id: device.parent,
      type: '',
      source_device_id: '',
    })
    try {
      types.value = (await deviceApi.types()).items
      parents.value = (await deviceApi.allDevices()).items
      editRevision = engineeringRevision()
    } catch (e) {
      error.value = errorMessage(e)
    }
  },
)
async function submit() {
  if (!form.name.trim() || !form.code.trim()) {
    error.value = '请填写名称和编码'
    return
  }
  busy.value = true
  error.value = ''
  try {
    const selected = types.value.find((t) => `${t.type_code}@${t.type_version}` === form.type)
    if (form.node_kind !== 'unit' && !selected) throw new Error('请选择设备类型')
    await deviceApi.create(
      id,
      {
        name: form.name.trim(),
        code: form.code.trim(),
        node_kind: form.node_kind,
        parent_device_id: form.parent_device_id,
        ...(form.node_kind === 'unit'
          ? {}
          : {
              device_type_code: selected!.type_code,
              type_version: selected!.type_version,
              source_device_id: form.node_kind === 'component' ? form.source_device_id : '',
            }),
        vendor: form.vendor,
        model: form.model,
        firmware_version: form.firmware_version,
        description: form.description,
        enabled: false,
      },
      editRevision,
    )
    device.createDrawerOpen = false
    await device.load()
    ElMessage.success('设备已保存')
    void router.push(
      form.node_kind === 'device' ? `/admin/devices/${id}/channels` : `/admin/hierarchy/${id}`,
    )
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
</script>
<template>
  <el-drawer
    v-model="device.createDrawerOpen"
    title="新建设备"
    size="560px"
    :close-on-click-modal="!busy"
    :before-close="
      (done: () => void) => {
        if (!busy) done()
      }
    "
    ><form id="new-device" class="flex flex-col gap-5" @submit.prevent="submit">
      <InfoBar v-if="error" :text="error" tone="danger" />
      <div class="grid grid-cols-2 gap-5">
        <FormField label="名称" v-slot="slot"
          ><el-input :id="slot.id" v-model="form.name" /></FormField
        ><FormField label="编码" v-slot="slot"
          ><el-input :id="slot.id" v-model="form.code" /></FormField
        ><FormField label="节点种类" v-slot="slot"
          ><el-select :id="slot.id" v-model="form.node_kind"
            ><el-option label="通信设备" value="device" /><el-option
              label="部件"
              value="component" /><el-option label="单元" value="unit" /></el-select></FormField
        ><FormField label="父节点" v-slot="slot"
          ><el-select :id="slot.id" v-model="form.parent_device_id"
            ><el-option
              v-for="p in parents"
              :key="p.device_id"
              :value="p.device_id"
              :label="p.name" /></el-select></FormField
        ><FormField v-if="form.node_kind !== 'unit'" label="设备类型" v-slot="slot"
          ><el-select :id="slot.id" v-model="form.type"
            ><el-option
              v-for="t in types"
              :key="String(t.type_code) + String(t.type_version)"
              :value="`${t.type_code}@${t.type_version}`"
              :label="`${t.name ?? t.type_code} / ${t.type_version}`" /></el-select></FormField
        ><FormField v-if="form.node_kind === 'component'" label="采集来源设备" v-slot="slot"
          ><el-select :id="slot.id" v-model="form.source_device_id"
            ><el-option
              v-for="p in parents.filter((p) => p.node_kind === 'device')"
              :key="p.device_id"
              :value="p.device_id"
              :label="p.name" /></el-select></FormField
        ><FormField
          v-for="f in [
            { key: 'vendor', label: '厂家' },
            { key: 'model', label: '型号' },
            { key: 'firmware_version', label: '固件版本' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="slot"
          ><el-input :id="slot.id" v-model="form[f.key]"
        /></FormField>
      </div>
      <FormField label="描述" v-slot="slot"
        ><el-input :id="slot.id" v-model="form.description" type="textarea"
      /></FormField>
      <p class="text-[12px] text-muted">
        新建设备默认停用；完成通道和点位配置后可启用。保存即生效。
      </p>
    </form>
    <template #footer
      ><div class="flex justify-end gap-3">
        <ActionBtn :disabled="busy" @click="device.createDrawerOpen = false">取消</ActionBtn
        ><ActionBtn form="new-device" type="submit" variant="primary" :loading="busy"
          >创建并配置</ActionBtn
        >
      </div></template
    ></el-drawer
  >
</template>
