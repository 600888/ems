<script setup lang="ts">
import { computed, ref, reactive, watch } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { deviceApi, stableId, engineeringRevision, revisionOf } from '@/api/device'
import { errorMessage } from '@/api/http'
import type { Point, PointType, Binding } from '@/types/engineering'
const route = useRoute(),
  id = computed(() => String(route.params.id)),
  kind = ref<PointType>('yc'),
  rows = ref<Point[]>([]),
  bindings = ref<Binding[]>([]),
  definitions = ref<Record<string, unknown>[]>([]),
  busy = ref(false),
  error = ref(''),
  dialog = ref(false),
  editing = ref(''),
  page = ref(1),
  total = ref(0)
const labels = { yc: '遥测', yx: '遥信', yk: '遥控', yt: '遥调' }
const form = reactive({
  code: '',
  name: '',
  binding_id: '',
  semantic_code: '',
  wire_type: 'i16',
  byte_order: 'ab',
  value_type: 'double',
  unit: '',
  enabled: false,
  disabled_reason: '',
  space: 'holding_register',
  offset: 0,
  register_count: 1,
  scale_factor: 1,
  offset_value: 0,
  fixed_raw_value: '0',
  write_mode: 'direct',
  description: '',
})
async function load() {
  busy.value = true
  error.value = ''
  try {
    const [p, d, b] = await Promise.all([
      deviceApi.points(id.value, kind.value, page.value),
      deviceApi.detail(id.value),
      deviceApi.inherent(id.value, kind.value),
    ])
    rows.value = p.items
    total.value = p.total
    definitions.value = b.items
    bindings.value = (
      await deviceApi.bindings(d.node_kind === 'component' ? d.source_device_id : id.value)
    ).items
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
function edit(row?: Point) {
  editing.value = row?.point_id ?? ''
  newPointId = stableId('pt')
  Object.assign(form, {
    code: '',
    name: '',
    binding_id: bindings.value[0]?.binding_id ?? '',
    semantic_code: '',
    wire_type: kind.value === 'yx' ? 'bool' : 'i16',
    byte_order: 'ab',
    value_type: kind.value === 'yx' || kind.value === 'yk' ? 'bool' : 'double',
    unit: '',
    enabled: false,
    disabled_reason: '待配置',
    space: kind.value === 'yx' || kind.value === 'yk' ? 'coil' : 'holding_register',
    offset: 0,
    register_count: 1,
    scale_factor: 1,
    offset_value: 0,
    fixed_raw_value: '0',
    write_mode: 'direct',
    description: '',
  })
  if (row) {
    for (const k of Object.keys(form)) if (k in row) (form as Record<string, unknown>)[k] = row[k]
    form.enabled = Boolean(row.enabled)
    form.space = String(row.address?.space ?? '')
    form.offset = Number(row.address?.offset ?? 0)
    form.register_count = Number(row.address?.register_count ?? 1)
  }
  editRevision = row ? revisionOf(row) : engineeringRevision()
  dialog.value = true
  error.value = ''
}
async function save() {
  busy.value = true
  try {
    const {
      space,
      offset,
      register_count,
      scale_factor,
      offset_value,
      fixed_raw_value,
      write_mode,
      code,
      ...common
    } = form
    const previous = rows.value.find((p) => p.point_id === editing.value)
    await deviceApi.savePoint(
      id.value,
      kind.value,
      editing.value || newPointId,
      {
        ...common,
        ...(!editing.value ? { code } : {}),
        address: { ...previous?.address, space, offset, register_count },
        ...(['yc', 'yt'].includes(kind.value) ? { scale_factor, offset_value } : {}),
        ...(kind.value === 'yk' ? { fixed_raw_value, write_mode } : {}),
      },
      !editing.value,
      editRevision,
    )
    dialog.value = false
    await load()
    ElMessage.success('点位已保存并生效')
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function remove(row: Point) {
  try {
    await ElMessageBox.confirm(`确认删除点位“${row.name}”？`, '删除点位')
  } catch {
    return
  }
  try {
    await deviceApi.removePoint(id.value, kind.value, row.point_id, revisionOf(row))
    await load()
  } catch (e) {
    error.value = errorMessage(e)
  }
}
watch([id, kind, page], load, { immediate: true })

let editRevision = ''
let newPointId = ''
</script>
<template>
  <div class="flex flex-col gap-4">
    <InfoBar v-if="error" :text="error" tone="danger" />
    <section class="panel flex min-h-0 flex-1 flex-col">
      <div class="flex shrink-0 flex-wrap items-center gap-4 border-b border-line p-4">
        <button
          v-for="(label, key) in labels"
          :key="key"
          :class="kind === key ? 'text-accent' : 'text-muted'"
          @click="
            () => {
              kind = key
              page = 1
            }
          "
        >
          {{ label }}</button
        ><ActionBtn class="ml-auto" :loading="busy" @click="load">刷新</ActionBtn
        ><ActionBtn variant="primary" @click="edit()">新增点位</ActionBtn>
      </div>
      <div class="min-h-0 flex-1 overflow-x-auto">
        <table class="identity-table">
          <thead>
            <tr>
              <th>名称 / 编码</th>
              <th>固有语义</th>
              <th>地址</th>
              <th>类型 / 单位</th>
              <th>状态</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in rows" :key="row.point_id">
              <td>
                {{ row.name }}
                <p class="text-small text-muted">{{ row.code }}</p>
              </td>
              <td>{{ row.semantic_code || '—' }}</td>
              <td>{{ row.address?.space }} / {{ row.address?.offset }}</td>
              <td>{{ row.wire_type }} → {{ row.value_type }} {{ row.unit }}</td>
              <td>{{ row.enabled ? '启用' : row.disabled_reason || '停用' }}</td>
              <td>
                <div class="flex gap-4">
                  <button class="identity-text-action" @click="edit(row)">编辑</button
                  ><button class="text-danger" @click="remove(row)">删除</button>
                </div>
              </td>
            </tr>
          </tbody>
        </table>
        <p v-if="!rows.length && !busy" class="p-8 text-muted">暂无{{ labels[kind] }}点位</p>
      </div>
      <el-pagination
        v-model:current-page="page"
        :page-size="200"
        :total="total"
        layout="prev, pager, next"
        class="p-4"
      />
    </section>
    <el-dialog
      v-model="dialog"
      :title="`${editing ? '编辑' : '新增'}${labels[kind]}点位`"
      width="700px"
      ><form id="point-form" class="grid grid-cols-2 gap-5" @submit.prevent="save">
        <InfoBar v-if="error" class="col-span-2" :text="error" tone="danger" /><FormField
          v-for="f in [
            { key: 'name', label: '名称' },
            { key: 'code', label: '编码' },
            { key: 'unit', label: '单位' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="s"
          ><el-input
            :id="s.id"
            v-model="form[f.key]"
            :disabled="f.key === 'code' && !!editing" /></FormField
        ><FormField label="绑定" v-slot="s"
          ><el-select :id="s.id" v-model="form.binding_id"
            ><el-option
              v-for="b in bindings"
              :key="b.binding_id"
              :value="b.binding_id"
              :label="`${b.channel?.name} / ${b.device_address}`" /></el-select></FormField
        ><FormField label="固有语义映射" v-slot="s"
          ><el-select :id="s.id" v-model="form.semantic_code" clearable
            ><el-option
              v-for="d in definitions"
              :key="String(d.semantic_code)"
              :value="String(d.semantic_code)"
              :label="`${d.name} · ${d.semantic_code}`" /></el-select></FormField
        ><FormField label="传输类型" v-slot="s"
          ><el-select :id="s.id" v-model="form.wire_type"
            ><el-option
              v-for="v in ['bool', 'i16', 'u16']"
              :key="v"
              :value="v"
              :label="v" /></el-select></FormField
        ><FormField label="值类型" v-slot="s"
          ><el-select :id="s.id" v-model="form.value_type"
            ><el-option
              v-for="v in kind === 'yc'
                ? ['double', 'int64', 'uint64']
                : kind === 'yx'
                  ? ['bool', 'int64', 'string']
                  : kind === 'yk'
                    ? ['bool', 'int64']
                    : ['double', 'int64']"
              :key="v"
              :label="v"
              :value="v" /></el-select></FormField
        ><FormField label="字节序" v-slot="s"
          ><el-select :id="s.id" v-model="form.byte_order"
            ><el-option label="AB" value="ab" /><el-option
              label="BA"
              value="ba" /></el-select></FormField
        ><FormField label="地址空间" v-slot="s"
          ><el-input :id="s.id" v-model="form.space" /></FormField
        ><FormField label="点表地址" v-slot="s"
          ><el-input-number :id="s.id" v-model="form.offset" :min="0" /></FormField
        ><template v-if="kind === 'yc' || kind === 'yt'"
          ><FormField label="倍率" v-slot="s"
            ><el-input-number :id="s.id" v-model="form.scale_factor" /></FormField
          ><FormField label="偏移量" v-slot="s"
            ><el-input-number :id="s.id" v-model="form.offset_value" /></FormField></template
        ><FormField v-if="kind === 'yk'" label="固定写入原始值" v-slot="s"
          ><el-input :id="s.id" v-model="form.fixed_raw_value" /></FormField
        ><label class="flex items-center gap-4">启用<el-switch v-model="form.enabled" /></label
        ><FormField v-if="!form.enabled" label="停用原因" v-slot="s"
          ><el-input :id="s.id" v-model="form.disabled_reason"
        /></FormField>
      </form>
      <template #footer
        ><ActionBtn type="submit" form="point-form" :loading="busy" variant="primary"
          >保存</ActionBtn
        ></template
      ></el-dialog
    >
  </div>
</template>
