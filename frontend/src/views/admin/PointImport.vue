<script setup lang="ts">
import { ref, computed, watch, reactive } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import { deviceApi, revisionOf } from '@/api/device'
import { errorMessage } from '@/api/http'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import type { Binding, ImportJob, ImportEntry } from '@/types/engineering'
const route = useRoute(),
  id = computed(() => String(route.params.id)),
  busy = ref(false),
  error = ref(''),
  bindings = ref<Binding[]>([]),
  binding = ref(''),
  jobs = ref<ImportJob[]>([]),
  job = ref<ImportJob | null>(null),
  rows = ref<ImportEntry[]>([]),
  page = ref(1),
  total = ref(0),
  file = ref<HTMLInputElement>(),
  note = ref(''),
  maxBytes = ref(0)
async function load() {
  busy.value = true
  error.value = ''
  try {
    const [b, j, c] = await Promise.all([
      deviceApi.bindings(id.value),
      deviceApi.imports(id.value),
      deviceApi.capabilities(),
    ])
    bindings.value = b.items
    jobs.value = j.items
    maxBytes.value = c.max_upload_bytes
    binding.value = b.items[0]?.binding_id ?? ''
    if (job.value) await select(job.value.import_job_id)
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function select(import_job_id: string) {
  try {
    job.value = await deviceApi.importDetail(import_job_id)
    const r = await deviceApi.importRows(import_job_id, page.value)
    rows.value = r.items
    total.value = r.total
    editRevision = revisionOf(job.value)
  } catch (e) {
    error.value = errorMessage(e)
  }
}
async function upload() {
  const selected = file.value?.files?.[0]
  if (!selected) return
  if (selected.size > maxBytes.value) {
    error.value = `文件不能超过 ${maxBytes.value} 字节`
    return
  }
  busy.value = true
  error.value = ''
  try {
    job.value = await deviceApi.upload(selected, {
      device_id: id.value,
      binding_id: binding.value,
      source_kind: selected.name.toLowerCase().endsWith('.csv') ? 'vendor_csv' : 'vendor_xlsx',
    })
    page.value = 1
    await load()
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
    if (file.value) file.value.value = ''
  }
}
async function confirm(row: ImportEntry, state: string) {
  if (!job.value) return
  if (!note.value.trim()) {
    error.value = '请填写确认说明'
    return
  }
  busy.value = true
  try {
    job.value = await deviceApi.updateImportRow({
      import_job_id: job.value.import_job_id,
      line_no: row.line_no,
      expected_preview_hash: job.value.preview_hash,
      fields: { confirm_state: state, confirm_note: note.value.trim() },
    })
    await select(job.value.import_job_id)
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function validate() {
  if (!job.value) return
  busy.value = true
  try {
    job.value = await deviceApi.validateImport(job.value.import_job_id, job.value.preview_hash)
    await select(job.value.import_job_id)
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function apply() {
  if (!job.value) return
  try {
    await ElMessageBox.confirm('将把已确认点位写入当前配置，保存后立即生效。', '应用点表')
  } catch {
    return
  }
  busy.value = true
  try {
    await deviceApi.applyImport(job.value.import_job_id, job.value.preview_hash, editRevision)
    ElMessage.success('点表已应用')
    await select(job.value.import_job_id)
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
watch(
  id,
  () => {
    job.value = null
    rows.value = []
    void load()
  },
  { immediate: true },
)
watch(page, () => {
  if (job.value) void select(job.value.import_job_id)
})

let editRevision = ''

const rowDialog = ref(false),
  editingLine = ref(0)
const correction = reactive({
  vendor_name: '',
  point_type: 'yc',
  semantic_code: '',
  target_device_path: '',
  address_space: '',
  address_offset: 0,
  bit_offset: -1,
  register_count: 1,
  wire_type: 'i16',
  byte_order: 'ab',
  value_type: 'double',
  scale_factor: 1,
  offset_value: 0,
  unit: '',
})
let previewAtEdit = ''
function editRow(row: ImportEntry) {
  editingLine.value = row.line_no
  const address = row.address as Record<string, unknown> | undefined
  for (const key of Object.keys(correction))
    if (key in row) (correction as Record<string, unknown>)[key] = row[key]
  correction.address_space = String(address?.space ?? row.address_space ?? '')
  correction.address_offset = Number(address?.offset ?? row.address_offset ?? 0)
  correction.bit_offset = Number(address?.bit_offset ?? row.bit_offset ?? -1)
  correction.register_count = Number(address?.register_count ?? row.register_count ?? 1)
  previewAtEdit = job.value!.preview_hash
  rowDialog.value = true
  error.value = ''
}
async function saveCorrection() {
  if (!job.value) return
  busy.value = true
  try {
    job.value = await deviceApi.updateImportRow({
      import_job_id: job.value.import_job_id,
      line_no: editingLine.value,
      expected_preview_hash: previewAtEdit,
      fields: { ...correction },
    })
    rowDialog.value = false
    await select(job.value.import_job_id)
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
</script>
<template>
  <div class="flex flex-col gap-4">
    <InfoBar v-if="error" :text="error" tone="danger" />
    <div class="panel flex shrink-0 flex-wrap items-center gap-3 p-4">
      <el-select v-model="binding" class="w-[240px]!" placeholder="采集绑定"
        ><el-option
          v-for="b in bindings"
          :key="b.binding_id"
          :value="b.binding_id"
          :label="`${b.channel?.name} / ${b.device_address}`" /></el-select
      ><input
        ref="file"
        type="file"
        accept=".csv,.xlsx"
        class="hidden"
        @change="upload"
      /><ActionBtn variant="primary" :disabled="!binding || busy" @click="file?.click()"
        >上传点表</ActionBtn
      ><ActionBtn :loading="busy" @click="load">刷新</ActionBtn>
    </div>
    <div class="panel flex shrink-0 flex-wrap gap-3 p-4">
      <el-select
        :model-value="job?.import_job_id"
        class="w-[400px]!"
        placeholder="选择导入任务"
        @update:model-value="
          (value: string) => {
            page = 1
            select(value)
          }
        "
        ><el-option
          v-for="j in jobs"
          :key="j.import_job_id"
          :value="j.import_job_id"
          :label="`${j.file_name} · ${j.status}`" /></el-select
      ><span v-if="job" class="self-center text-small text-muted">{{ job.status }}</span>
    </div>
    <template v-if="job"
      ><InfoBar v-if="job.error_message" :text="job.error_message" tone="warn" />
      <section class="panel flex min-h-0 flex-1 flex-col">
        <div class="flex shrink-0 flex-wrap gap-3 p-4">
          <el-input
            v-model="note"
            placeholder="确认或排除行的说明"
            class="max-w-[420px]"
          /><ActionBtn
            :disabled="busy || !job.allowed_operations.includes('validate')"
            @click="validate"
            >校验</ActionBtn
          ><ActionBtn
            variant="primary"
            :disabled="busy || job.status !== 'confirmed'"
            @click="apply"
            >应用点表</ActionBtn
          >
        </div>
        <div class="min-h-0 flex-1 overflow-x-auto">
          <table class="identity-table">
            <thead>
              <tr>
                <th>行号</th>
                <th>名称</th>
                <th>分类</th>
                <th>语义</th>
                <th>解析结果</th>
                <th>确认</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in rows" :key="row.line_no">
                <td>{{ row.line_no }}</td>
                <td>{{ row.vendor_name }}</td>
                <td>{{ row.point_type }}</td>
                <td>{{ row.semantic_code }}</td>
                <td>{{ row.parse_status }} {{ row.parse_message }}</td>
                <td>{{ row.confirm_state }}</td>
                <td>
                  <div v-if="job.allowed_operations.includes('edit_rows')" class="flex gap-3">
                    <button :disabled="busy" class="identity-text-action" @click="editRow(row)">
                      修正</button
                    ><button
                      :disabled="busy"
                      class="identity-text-action"
                      @click="confirm(row, 'accepted')"
                    >
                      确认</button
                    ><button
                      :disabled="busy"
                      class="identity-text-action"
                      @click="confirm(row, 'rejected')"
                    >
                      排除
                    </button>
                  </div>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
        <el-pagination
          v-model:current-page="page"
          :page-size="200"
          :total="total"
          layout="prev, pager, next"
          class="p-4"
        /></section
    ></template>
    <p v-else class="panel p-8 text-muted">
      上传厂家 CSV/XLSX 点表后预览并确认；只有点击应用才会修改当前点位。
    </p>
    <el-dialog v-model="rowDialog" :title="`修正第 ${editingLine} 行`" width="720px"
      ><form id="import-correction" class="grid grid-cols-2 gap-5" @submit.prevent="saveCorrection">
        <InfoBar v-if="error" class="col-span-2" :text="error" tone="danger" /><FormField
          v-for="f in [
            { key: 'vendor_name', label: '测点名称' },
            { key: 'semantic_code', label: '固有语义编码' },
            { key: 'target_device_path', label: '目标设备路径' },
            { key: 'address_space', label: '地址空间' },
            { key: 'unit', label: '单位' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="field"
          ><el-input :id="field.id" v-model="correction[f.key]" /></FormField
        ><FormField label="四遥分类" v-slot="field"
          ><el-select :id="field.id" v-model="correction.point_type"
            ><el-option
              v-for="v in ['yc', 'yx', 'yk', 'yt']"
              :key="v"
              :value="v"
              :label="v" /></el-select></FormField
        ><FormField
          v-for="f in [
            { key: 'address_offset', label: '点表地址' },
            { key: 'bit_offset', label: '位偏移（-1 表示整值）' },
            { key: 'register_count', label: '寄存器数量' },
            { key: 'scale_factor', label: '倍率' },
            { key: 'offset_value', label: '偏移量' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="field"
          ><el-input-number :id="field.id" v-model="correction[f.key]" /></FormField
        ><FormField label="传输类型" v-slot="field"
          ><el-select :id="field.id" v-model="correction.wire_type"
            ><el-option
              v-for="v in ['bool', 'i16', 'u16', 'i32', 'u32', 'f32']"
              :key="v"
              :value="v"
              :label="v" /></el-select></FormField
        ><FormField label="值类型" v-slot="field"
          ><el-select :id="field.id" v-model="correction.value_type"
            ><el-option
              v-for="v in ['bool', 'int64', 'uint64', 'double', 'string']"
              :key="v"
              :value="v"
              :label="v" /></el-select></FormField
        ><FormField label="字节序" v-slot="field"
          ><el-select :id="field.id" v-model="correction.byte_order"
            ><el-option label="默认" value="" /><el-option
              v-for="v in ['ab', 'ba', 'ABCD', 'BADC', 'CDAB', 'DCBA']"
              :key="v"
              :label="v.toUpperCase()"
              :value="v" /></el-select
        ></FormField>
      </form>
      <template #footer
        ><ActionBtn type="submit" form="import-correction" variant="primary" :loading="busy"
          >保存修正</ActionBtn
        ></template
      ></el-dialog
    >
  </div>
</template>
