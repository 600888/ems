<script setup lang="ts">
import { ref, reactive, watch, computed } from 'vue'
import { useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { deviceApi, stableId, engineeringRevision, revisionOf } from '@/api/device'
import { errorMessage } from '@/api/http'
import type { Binding, Channel } from '@/types/engineering'
const route = useRoute(),
  id = computed(() => String(route.params.id)),
  bindings = ref<Binding[]>([]),
  channels = ref<Channel[]>([]),
  protocols = ref<string[]>([]),
  busy = ref(false),
  error = ref(''),
  dialog = ref(false),
  channelDialog = ref(false),
  editing = ref('')
const form = reactive({
  channel_id: '',
  device_address: '',
  profile_version: '',
  can_collect: 1,
  can_control: 0,
  can_forward: 0,
  enabled: 0,
  description: '',
})
const connection = reactive({
  channel_id: '',
  code: '',
  name: '',
  protocol: 'modbus',
  conn_mode: 'tcp_client',
  endpoint: '',
  poll_interval_ms: 1000,
  timeout_ms: 3000,
  retry_times: 2,
  enabled: 0,
  description: '',
})
async function load() {
  busy.value = true
  error.value = ''
  try {
    const [b, c, cap] = await Promise.all([
      deviceApi.bindings(id.value),
      deviceApi.channels(),
      deviceApi.capabilities(),
    ])
    bindings.value = b.items
    channels.value = c.items
    protocols.value = cap.protocols
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
function edit(row?: Binding) {
  editing.value = row?.binding_id ?? ''
  newBindingId = stableId('bind')
  Object.assign(
    form,
    row
      ? Object.fromEntries(
          Object.keys(form).map((k) => [
            k,
            typeof row[k as keyof Binding] === 'boolean'
              ? Number(row[k as keyof Binding])
              : row[k as keyof Binding],
          ]),
        )
      : {
          channel_id: channels.value[0]?.channel_id ?? '',
          device_address: '',
          profile_version: '',
          can_collect: 1,
          can_control: 0,
          can_forward: 0,
          enabled: 0,
          description: '',
        },
  )
  editRevision = row ? revisionOf(row) : engineeringRevision()
  dialog.value = true
  error.value = ''
}
async function save() {
  busy.value = true
  try {
    if (editing.value) {
      const { channel_id: _, ...fields } = form
      await deviceApi.updateBinding(id.value, editing.value, fields, editRevision)
    } else await deviceApi.createBinding(id.value, newBindingId, { ...form }, editRevision)
    dialog.value = false
    await load()
    ElMessage.success('绑定已保存并生效')
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
async function remove(row: Binding) {
  try {
    await ElMessageBox.confirm('确认删除绑定？已被点位引用时无法删除。', '删除绑定')
  } catch {
    return
  }
  try {
    await deviceApi.removeBinding(id.value, row.binding_id, revisionOf(row))
    await load()
  } catch (e) {
    error.value = errorMessage(e)
  }
}
function editChannel(row?: Channel) {
  newChannelId = stableId('ch')
  Object.assign(
    connection,
    row
      ? Object.fromEntries(
          Object.keys(connection).map((k) => [
            k,
            typeof row[k as keyof Channel] === 'boolean'
              ? Number(row[k as keyof Channel])
              : row[k as keyof Channel],
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
          enabled: 0,
          description: '',
        },
  )
  editRevision = row
    ? revisionOf(channels.value.find((channel) => channel.channel_id === row.channel_id) ?? row)
    : engineeringRevision()
  channelDialog.value = true
  error.value = ''
}
async function saveChannel() {
  busy.value = true
  try {
    const { channel_id, code, protocol, conn_mode, ...values } = connection
    if (channel_id) await deviceApi.updateChannel(channel_id, values, editRevision)
    else
      await deviceApi.createChannel(
        newChannelId,
        { code, protocol, conn_mode, ...values },
        editRevision,
      )
    channelDialog.value = false
    await load()
    ElMessage.success('通道已保存并生效')
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
watch(id, load, { immediate: true })

let editRevision = ''
let newBindingId = '',
  newChannelId = ''
</script>
<template>
  <div class="flex flex-col gap-4">
    <InfoBar v-if="error" :text="error" tone="danger" />
    <section class="panel">
      <div class="flex flex-wrap gap-3 p-4">
        <h3 class="mr-auto text-lead font-semibold">设备通道绑定</h3>
        <ActionBtn :loading="busy" @click="load">刷新</ActionBtn
        ><ActionBtn @click="editChannel()">新建通道</ActionBtn
        ><ActionBtn variant="primary" @click="edit()">新增绑定</ActionBtn>
      </div>
      <div class="overflow-x-auto">
        <table class="identity-table">
          <thead>
            <tr>
              <th>通道</th>
              <th>协议 / 地址</th>
              <th>设备地址</th>
              <th>采集 / 控制</th>
              <th>启用</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in bindings" :key="row.binding_id">
              <td>{{ row.channel?.name }}</td>
              <td>{{ row.channel?.protocol }} / {{ row.channel?.endpoint }}</td>
              <td>{{ row.device_address }}</td>
              <td>{{ row.can_collect ? '采集' : '' }} {{ row.can_control ? '控制' : '' }}</td>
              <td>{{ row.enabled ? '是' : '否' }}</td>
              <td>
                <div class="flex gap-4">
                  <button class="identity-text-action" @click="edit(row)">编辑绑定</button
                  ><button class="identity-text-action" @click="editChannel(row.channel)">
                    编辑通道</button
                  ><button class="text-danger" @click="remove(row)">删除绑定</button>
                </div>
              </td>
            </tr>
          </tbody>
        </table>
        <p v-if="!bindings.length" class="p-8 text-muted">暂无绑定</p>
      </div>
    </section>
    <el-dialog v-model="dialog" :title="editing ? '编辑绑定' : '新增绑定'" width="560px"
      ><form id="binding-form" class="flex flex-col gap-4" @submit.prevent="save">
        <InfoBar v-if="error" :text="error" tone="danger" /><FormField label="通道" v-slot="s"
          ><el-select :id="s.id" v-model="form.channel_id" :disabled="!!editing"
            ><el-option
              v-for="c in channels"
              :key="c.channel_id"
              :value="c.channel_id"
              :label="`${c.name} · ${c.protocol}`" /></el-select></FormField
        ><FormField label="设备地址" v-slot="s"
          ><el-input
            :id="s.id"
            v-model="form.device_address"
            placeholder="站号 / Unit ID" /></FormField
        ><FormField label="协议 Profile" v-slot="s"
          ><el-input :id="s.id" v-model="form.profile_version" /></FormField
        ><label
          v-for="f in [
            { key: 'can_collect', label: '允许采集' },
            { key: 'can_control', label: '允许控制' },
            { key: 'enabled', label: '启用绑定' },
          ] as const"
          :key="f.key"
          class="flex justify-between"
          >{{ f.label }}<el-switch v-model="form[f.key]" :active-value="1" :inactive-value="0"
        /></label>
      </form>
      <template #footer
        ><ActionBtn type="submit" form="binding-form" variant="primary" :loading="busy"
          >保存</ActionBtn
        ></template
      ></el-dialog
    >
    <el-dialog
      v-model="channelDialog"
      :title="connection.channel_id ? '编辑通道' : '新建通道'"
      width="600px"
      ><form id="channel-form" class="grid grid-cols-2 gap-5" @submit.prevent="saveChannel">
        <InfoBar v-if="error" class="col-span-2" :text="error" tone="danger" /><FormField
          v-for="f in [
            { key: 'code', label: '通道编码' },
            { key: 'name', label: '名称' },
            { key: 'endpoint', label: '远端地址' },
            { key: 'description', label: '描述' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="s"
          ><el-input
            :id="s.id"
            v-model="connection[f.key]"
            :disabled="f.key === 'code' && !!connection.channel_id" /></FormField
        ><FormField label="协议" v-slot="s"
          ><el-select :id="s.id" v-model="connection.protocol" :disabled="!!connection.channel_id"
            ><el-option
              v-for="p in protocols"
              :key="p"
              :label="p"
              :value="p" /></el-select></FormField
        ><FormField label="连接方式" v-slot="s"
          ><el-select :id="s.id" v-model="connection.conn_mode" :disabled="!!connection.channel_id"
            ><el-option label="TCP 客户端" value="tcp_client" /><el-option
              label="TCP 服务端"
              value="tcp_server" /><el-option label="串口" value="serial" /></el-select></FormField
        ><FormField
          v-for="f in [
            { key: 'poll_interval_ms', label: '轮询间隔 ms' },
            { key: 'timeout_ms', label: '超时 ms' },
            { key: 'retry_times', label: '重试次数' },
          ] as const"
          :key="f.key"
          :label="f.label"
          v-slot="s"
          ><el-input-number :id="s.id" v-model="connection[f.key]" :min="0" /></FormField
        ><label
          >启用通道 <el-switch v-model="connection.enabled" :active-value="1" :inactive-value="0"
        /></label>
      </form>
      <template #footer
        ><ActionBtn type="submit" form="channel-form" variant="primary" :loading="busy"
          >保存</ActionBtn
        ></template
      ></el-dialog
    >
  </div>
</template>
