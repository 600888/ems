<script setup lang="ts">
import { ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
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
  result.value = null
  try {
    const inspection = await deviceApi.inspectStationPackage(selected)
    if (!open.value) return
    if (!inspection.station_code_match) {
      try {
        await ElMessageBox.confirm(
          `包内站编码为“${inspection.package_station_code}”，当前站编码为“${inspection.station_code}”。是否继续导入到当前站？`,
          '站编码不一致',
          { type: 'warning', confirmButtonText: '继续导入', cancelButtonText: '取消' },
        )
      } catch {
        return
      }
    }
    if (!open.value) return
    result.value = (await deviceApi.upload(selected, {}, true)) as unknown as Record<
      string,
      unknown
    >
    ElMessage.success('整站设备和点位已导入并生效')
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
      <p class="text-body text-muted">
        上传包含 manifest.json 和设备 XLSX 点表的 ZIP
        文件。校验通过后一次保存设备、通道和全部点位，导入完成后生效。
      </p>
      <input ref="file" type="file" accept=".zip" :disabled="busy" @change="upload" />
      <div v-if="result" class="max-h-[320px] overflow-auto">
        <p class="text-body">
          已处理 {{ result.devices_created }} 个设备节点、{{ result.channels_created }} 个通道、{{
            result.bindings_created
          }}
          个绑定，已导入 {{ result.points_total }} 个点位。
        </p>
        <div
          v-for="entry in result.import_jobs as {
            device_id: string
            device_code: string
            target_device_id: string
            target_device_code: string
            row_total: number
            status: string
            import_job_id: string
          }[]"
          :key="entry.import_job_id"
          class="mt-4 flex items-center justify-between border-b border-line py-3"
        >
          <span>{{ entry.target_device_code }} · {{ entry.row_total }} 个点位已导入</span
          ><ActionBtn
            @click="
              () => {
                open = false
                router.push(`/admin/devices/${entry.target_device_id}/points/mapping`)
              }
            "
            >查看点位</ActionBtn
          >
        </div>
      </div>
    </div>
    <template #footer
      ><ActionBtn :loading="busy" @click="open = false">关闭</ActionBtn></template
    ></el-dialog
  >
</template>
