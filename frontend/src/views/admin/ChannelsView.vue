<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { deviceApi, stableId, engineeringRevision, revisionOf } from '@/api/device'
import { errorMessage } from '@/api/http'
import type { Channel } from '@/types/engineering'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AdminPageActions from '@/components/layout/AdminPageActions.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
const rows = ref<Channel[]>([]),
  protocols = ref<string[]>([]),
  error = ref(''),
  busy = ref(false),
  dialog = ref(false)
const form = reactive({
  channel_id: '',
  code: '',
  name: '',
  protocol: 'modbus',
  conn_mode: 'tcp_client',
  endpoint: '',
  poll_interval_ms: 1000,
  timeout_ms: 3000,
  retry_times: 2,
  enabled: false,
  description: '',
})
let expected = '',
  createdId = ''
async function load() {
  busy.value = true
  error.value = ''
  try {
    const [r, c] = await Promise.all([deviceApi.channels(), deviceApi.capabilities()])
    rows.value = r.items
    protocols.value = c.protocols
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
function edit(row?: Channel) {
  createdId = stableId('ch')
  expected = row ? revisionOf(row) : engineeringRevision()
  Object.assign(
    form,
    row
      ? Object.fromEntries(
          Object.keys(form).map((key) => [
            key,
            key === 'enabled' ? Boolean(row.enabled) : row[key as keyof Channel],
          ]),
        )
      : {
          channel_id: '',
          code: '',
          name: '',
          protocol: protocols.value[0] ?? 'modbus',
          conn_mode: 'tcp_client',
          endpoint: '',
          poll_interval_ms: 1000,
          timeout_ms: 3000,
          retry_times: 2,
          enabled: false,
          description: '',
        },
  )
  dialog.value = true
  error.value = ''
}
async function save() {
  busy.value = true
  try {
    const { channel_id, code, protocol, conn_mode, ...fields } = form
    if (channel_id) await deviceApi.updateChannel(channel_id, fields, expected)
    else
      await deviceApi.createChannel(createdId, { code, protocol, conn_mode, ...fields }, expected)
    dialog.value = false
    await load()
    ElMessage.success('通道已保存并生效')
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function remove(row: Channel) {
  const revision = revisionOf(row)
  try {
    await ElMessageBox.confirm(`确认删除通道“${row.name}”？`, '删除通道')
  } catch {
    return
  }
  try {
    await deviceApi.removeChannel(row.channel_id, revision)
    await load()
  } catch (e) {
    error.value = errorMessage(e)
  }
}
onMounted(load)
</script>
<template>
  <div class="identity-page">
    <InfoBar v-if="error" :text="error" tone="danger" />
    <AdminPageActions>
      <ActionBtn icon="refresh-cw" :loading="busy" @click="load">刷新</ActionBtn>
      <ActionBtn variant="primary" icon="plus" @click="edit()">新建通道</ActionBtn>
    </AdminPageActions>
    <section class="panel min-h-0 flex-1 overflow-x-auto">
      <table class="identity-table">
        <thead>
          <tr>
            <th>名称 / 编码</th>
            <th>协议</th>
            <th>连接方式</th>
            <th>远端地址</th>
            <th>启用</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in rows" :key="row.channel_id">
            <td>{{ row.name }} / {{ row.code }}</td>
            <td>{{ row.protocol }}</td>
            <td>{{ row.conn_mode }}</td>
            <td>{{ row.endpoint }}</td>
            <td>{{ row.enabled ? '是' : '否' }}</td>
            <td>
              <div class="flex gap-4">
                <button class="identity-text-action" @click="edit(row)">编辑</button
                ><button class="text-danger" @click="remove(row)">删除</button>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
      <p v-if="!rows.length && !busy" class="p-8 text-muted">暂无通信通道</p>
    </section>
    <el-dialog v-model="dialog" :title="form.channel_id ? '编辑通道' : '新建通道'" width="620px"
      ><form id="global-channel" class="grid grid-cols-2 gap-5" @submit.prevent="save">
        <InfoBar v-if="error" class="col-span-2" :text="error" tone="danger" /><FormField
          v-for="f in [
            { key: 'code', label: '编码' },
            { key: 'name', label: '名称' },
            { key: 'endpoint', label: '远端地址' },
            { key: 'description', label: '描述' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="slot"
          ><el-input
            :id="slot.id"
            v-model="form[f.key]"
            :disabled="f.key === 'code' && !!form.channel_id" /></FormField
        ><FormField label="协议" v-slot="slot"
          ><el-select :id="slot.id" v-model="form.protocol" :disabled="!!form.channel_id"
            ><el-option
              v-for="p in protocols"
              :key="p"
              :value="p"
              :label="p" /></el-select></FormField
        ><FormField label="连接方式" v-slot="slot"
          ><el-select :id="slot.id" v-model="form.conn_mode" :disabled="!!form.channel_id"
            ><el-option label="TCP 客户端" value="tcp_client" /><el-option
              label="TCP 服务端"
              value="tcp_server" /><el-option label="串口" value="serial" /></el-select></FormField
        ><FormField
          v-for="f in [
            { key: 'poll_interval_ms', label: '轮询 ms' },
            { key: 'timeout_ms', label: '超时 ms' },
            { key: 'retry_times', label: '重试次数' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="slot"
          ><el-input-number :id="slot.id" v-model="form[f.key]" :min="0" /></FormField
        ><label>启用 <el-switch v-model="form.enabled" /></label>
      </form>
      <template #footer
        ><ActionBtn type="submit" form="global-channel" :loading="busy" variant="primary"
          >保存</ActionBtn
        ></template
      ></el-dialog
    >
  </div>
</template>
