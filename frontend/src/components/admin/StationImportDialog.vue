<script setup lang="ts">
import { ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useRouter } from 'vue-router'
import { deviceApi } from '@/api/device'
import { errorMessage } from '@/api/http'
import { useDeviceStore } from '@/stores/device'
import ActionBtn from '@/components/common/ActionBtn.vue'
import InfoBar from '@/components/common/InfoBar.vue'
const router = useRouter()
const open = defineModel<boolean>({ default: false }),
  device = useDeviceStore(),
  file = ref<HTMLInputElement>(),
  busy = ref(false),
  error = ref(''),
  result = ref<Record<string, unknown> | null>(null)
watch(open, () => {
  error.value = ''
  result.value = null
})
async function upload() {
  const selected = file.value?.files?.[0]
  if (!selected) return
  busy.value = true
  error.value = ''
  try {
    result.value = (await deviceApi.upload(selected, {}, true)) as unknown as Record<
      string,
      unknown
    >
    ElMessage.success('整站包已解析，设备和绑定已保存；请逐设备确认并应用点表')
    await device.load()
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
    if (file.value) file.value.value = ''
  }
}
</script>
<template>
  <el-dialog v-model="open" title="整站包导入" width="700px"
    ><div class="flex flex-col gap-4">
      <InfoBar v-if="error" :text="error" tone="danger" />
      <p class="text-[13px] text-muted">
        上传包含 manifest.json 和设备 XLSX 点表的 ZIP
        文件。设备及通道保存后生效；点表需进入各设备导入任务确认后应用。
      </p>
      <input ref="file" type="file" accept=".zip" :disabled="busy" @change="upload" />
      <div v-if="result" class="max-h-[320px] overflow-auto">
        <p class="text-[13px]">
          已处理 {{ result.devices_created }} 个设备节点、{{ result.channels_created }} 个通道、{{
            result.bindings_created
          }}
          个绑定。
        </p>
        <div
          v-for="entry in result.import_jobs as {
            device_id: string
            device_code: string
            status: string
            import_job_id: string
          }[]"
          :key="entry.import_job_id"
          class="mt-4 flex items-center justify-between border-b border-line py-3"
        >
          <span>{{ entry.device_code }} · {{ entry.status }}</span
          ><ActionBtn
            @click="
              () => {
                open = false
                router.push(`/admin/devices/${entry.device_id}/points/import`)
              }
            "
            >确认点表</ActionBtn
          >
        </div>
      </div>
    </div>
    <template #footer
      ><ActionBtn :loading="busy" @click="open = false">关闭</ActionBtn></template
    ></el-dialog
  >
</template>
