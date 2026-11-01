<script setup lang="ts">
import { computed, onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import AdminDeviceTree from '@/components/device/AdminDeviceTree.vue'
import DeviceToolbar from '@/components/admin/DeviceToolbar.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import { useDeviceStore } from '@/stores/device'
import { deviceApi, revisionOf } from '@/api/device'
import { errorMessage } from '@/api/http'
import type { EngineeringRecord } from '@/types/engineering'
const device = useDeviceStore(),
  router = useRouter(),
  route = useRoute()
const stats = computed(() => [
  { label: '当前结果', value: device.total },
  { label: '本页设备', value: device.rows.filter((r) => r.node_kind === 'device').length },
  { label: '本页部件', value: device.rows.filter((r) => r.node_kind === 'component').length },
  {
    label: '本页待补充',
    value: device.rows.filter((r) => r.config_completeness === 'incomplete').length,
  },
])
function open(row: EngineeringRecord) {
  void router.push(
    row.node_kind === 'device'
      ? `/admin/devices/${row.device_id}/basic`
      : `/admin/hierarchy/${row.device_id}`,
  )
}
async function remove(row: EngineeringRecord) {
  try {
    await ElMessageBox.confirm(`确认删除“${row.name}”？关联配置也会受影响。`, '删除设备', {
      type: 'warning',
    })
  } catch {
    return
  }
  try {
    await deviceApi.remove(row.device_id, revisionOf(row))
    ElMessage.success('设备已删除')
    await device.load()
  } catch (cause) {
    ElMessage.error(errorMessage(cause))
  }
}
function search() {
  device.page = 1
  void device.load()
}
function exportRows() {
  const fields = ['name', 'code', 'node_kind', 'device_type_code', 'vendor', 'model'] as const
  const csv = [
    fields.join(','),
    ...device.rows.map((r) =>
      fields.map((k) => `"${String(r[k] ?? '').replaceAll('"', '""')}"`).join(','),
    ),
  ].join('\r\n')
  const url = URL.createObjectURL(new Blob(['\ufeff' + csv], { type: 'text/csv;charset=utf-8' }))
  const a = document.createElement('a')
  a.href = url
  a.download = '设备列表.csv'
  a.click()
  URL.revokeObjectURL(url)
}
onMounted(() => {
  void device.load()
  if (route.query.new) device.createDrawerOpen = true
})
</script>
<template>
  <div class="flex min-h-0 flex-1 gap-5">
    <AdminDeviceTree />
    <section class="flex min-w-0 flex-1 flex-col gap-[18px]">
      <DeviceToolbar
        ><span class="text-muted">{{ device.tree[0]?.name ?? '设备目录' }}</span></DeviceToolbar
      >
      <div class="grid shrink-0 grid-cols-4 gap-3">
        <article v-for="item in stats" :key="item.label" class="panel p-4">
          <p class="text-[12px] text-muted">{{ item.label }}</p>
          <p class="mt-2 text-[26px] text-accent">{{ item.value }}</p>
        </article>
      </div>
      <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
        <form class="flex shrink-0 flex-wrap gap-3 p-4" @submit.prevent="search">
          <el-input
            v-model="device.keyword"
            class="max-w-[330px]"
            placeholder="搜索设备名称 / 编码"
            clearable
          /><ActionBtn type="submit" variant="primary">查询</ActionBtn
          ><ActionBtn :loading="device.loading" @click="device.load">刷新</ActionBtn
          ><ActionBtn class="ml-auto" icon="download" @click="exportRows">导出本页</ActionBtn>
        </form>
        <AsyncContent
          :loading="device.loading"
          :error="device.error"
          :empty="!device.rows.length"
          empty-title="暂无设备"
          @retry="device.load"
          ><div class="min-h-0 flex-1 overflow-auto">
            <table class="identity-table">
              <thead>
                <tr>
                  <th>设备名称</th>
                  <th>编码</th>
                  <th>节点 / 类型</th>
                  <th>厂家 / 型号</th>
                  <th>固有点位</th>
                  <th>配置状态</th>
                  <th>操作</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="row in device.rows" :key="row.device_id">
                  <td>{{ row.name }}</td>
                  <td>{{ row.code }}</td>
                  <td>{{ row.node_kind }} / {{ row.device_type_code || '—' }}</td>
                  <td>{{ row.vendor || '—' }} / {{ row.model || '—' }}</td>
                  <td>
                    {{ row.inherent_points?.enabled ?? 0 }} / {{ row.inherent_points?.total ?? 0 }}
                  </td>
                  <td>{{ row.config_completeness === 'incomplete' ? '待补充' : '完整' }}</td>
                  <td>
                    <div class="flex gap-4">
                      <button class="identity-text-action" @click="open(row)">查看配置</button
                      ><button
                        v-if="row.node_kind !== 'station'"
                        class="text-danger"
                        @click="remove(row)"
                      >
                        删除
                      </button>
                    </div>
                  </td>
                </tr>
              </tbody>
            </table>
          </div></AsyncContent
        >
        <footer class="flex shrink-0 items-center justify-between border-t border-line p-4">
          <span class="text-[12px] text-muted">共 {{ device.total }} 项</span
          ><el-pagination
            v-model:current-page="device.page"
            :page-size="device.pageSize"
            :total="device.total"
            layout="prev, pager, next"
            @current-change="device.load"
          />
        </footer>
      </section>
    </section>
  </div>
</template>
