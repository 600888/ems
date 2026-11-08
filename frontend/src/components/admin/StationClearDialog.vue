<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useRouter } from 'vue-router'
import { deviceApi, revisionOf } from '@/api/device'
import { errorMessage } from '@/api/http'
import { useDeviceStore } from '@/stores/device'
import type { EngineeringRecord } from '@/types/engineering'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'

const open = defineModel<boolean>({ default: false })
const router = useRouter()
const device = useDeviceStore()
const station = ref<EngineeringRecord | null>(null)
const loading = ref(false),
  busy = ref(false),
  error = ref('')
const confirmation = ref(''),
  code = ref(''),
  name = ref('')
let expected = ''
const ready = computed(
  () =>
    !loading.value &&
    station.value &&
    confirmation.value === station.value.code &&
    /^[A-Za-z0-9_-]{1,64}$/.test(code.value) &&
    name.value.trim().length > 0,
)
watch(open, async (visible, _, onCleanup) => {
  if (!visible) return
  let active = true
  onCleanup(() => {
    active = false
  })
  station.value = null
  confirmation.value = ''
  code.value = ''
  name.value = ''
  expected = ''
  error.value = ''
  loading.value = true
  try {
    const config = await deviceApi.configuration()
    const row = await deviceApi.detail(config.station_device_id)
    if (!active) return
    station.value = row
    code.value = row.code
    name.value = row.name
    expected = revisionOf(row)
  } catch (cause) {
    if (active) error.value = errorMessage(cause)
  } finally {
    if (active) loading.value = false
  }
})
async function clear() {
  if (!ready.value || !station.value || busy.value) return
  busy.value = true
  error.value = ''
  try {
    await deviceApi.clearStation(
      station.value.device_id,
      confirmation.value,
      code.value,
      name.value.trim(),
      expected,
    )
  } catch (cause) {
    error.value = errorMessage(cause)
    busy.value = false
    return
  }
  ElMessage.success('整站配置已清空，可以导入新站配置')
  open.value = false
  try {
    await router.replace('/admin/devices')
    await device.reloadStation()
  } catch {
    ElMessage.warning('整站配置已清空，请刷新页面后导入新站配置')
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <el-dialog
    v-model="open"
    title="清空整站配置"
    width="600px"
    :close-on-click-modal="!busy"
    :close-on-press-escape="!busy"
    :show-close="!busy"
  >
    <form class="flex flex-col gap-5" @submit.prevent="clear">
      <InfoBar
        tone="danger"
        text="此操作不可撤销：将删除全部下级设备、部件、通道、绑定、四遥点位、导入记录及通信统计和状态记录。请确认现场允许停止当前配置的采集与控制。"
      />
      <p class="text-body text-muted">
        保留站根、账号、审计日志、设备类型和型号模板。清空后可直接整站导入；也可在此填写新站名称与编码。
      </p>
      <InfoBar v-if="error" :text="error" tone="danger" />
      <p v-if="loading" class="text-body text-muted">正在读取当前站配置…</p>
      <template v-if="station">
        <p class="text-body">当前站：{{ station.name }}（{{ station.code }}）</p>
        <FormField label="清空后的站名称" v-slot="slot">
          <el-input :id="slot.id" v-model="name" :disabled="busy" maxlength="128" />
        </FormField>
        <FormField label="清空后的站编码" v-slot="slot">
          <el-input :id="slot.id" v-model="code" :disabled="busy" maxlength="64" />
        </FormField>
        <FormField :label="`输入当前站编码 ${station.code} 确认清空`" v-slot="slot">
          <el-input :id="slot.id" v-model="confirmation" :disabled="busy" autocomplete="off" />
        </FormField>
      </template>
      <div class="flex justify-end gap-3">
        <ActionBtn :disabled="busy" @click="open = false">取消</ActionBtn>
        <ActionBtn type="submit" variant="danger" :disabled="!ready" :loading="busy"
          >确认清空整站</ActionBtn
        >
      </div>
    </form>
  </el-dialog>
</template>
