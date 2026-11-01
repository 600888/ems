<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { deviceApi } from '@/api/device'
import { errorMessage } from '@/api/http'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
const types = ref<Record<string, unknown>[]>([]),
  templates = ref<Record<string, unknown>[]>([]),
  busy = ref(false),
  error = ref('')
async function load() {
  busy.value = true
  error.value = ''
  try {
    const [t, m] = await Promise.all([deviceApi.types(), deviceApi.templates()])
    types.value = t.items
    templates.value = m.items
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
onMounted(load)
</script>
<template>
  <div class="identity-page">
    <div class="flex shrink-0 justify-end">
      <ActionBtn :loading="busy" @click="load">刷新</ActionBtn>
    </div>
    <AsyncContent :loading="busy" :error="error" @retry="load"
      ><section class="panel overflow-auto">
        <h2 class="p-4 text-[17px] font-semibold">设备类型</h2>
        <table class="identity-table">
          <thead>
            <tr>
              <th>类型</th>
              <th>名称</th>
              <th>业务版本</th>
              <th>类别</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="t in types" :key="`${t.type_code}@${t.type_version}`">
              <td>{{ t.type_code }}</td>
              <td>{{ t.name }}</td>
              <td>{{ t.type_version }}</td>
              <td>{{ t.category }}</td>
              <td>{{ t.status }}</td>
            </tr>
          </tbody>
        </table>
      </section>
      <section class="panel overflow-auto">
        <h2 class="p-4 text-[17px] font-semibold">型号模板</h2>
        <table class="identity-table">
          <thead>
            <tr>
              <th>模板</th>
              <th>厂家</th>
              <th>型号</th>
              <th>设备类型</th>
              <th>模板业务版本</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="t in templates" :key="`${t.template_id}@${t.template_version}`">
              <td>{{ t.name ?? t.template_id }}</td>
              <td>{{ t.vendor }}</td>
              <td>{{ t.model }}</td>
              <td>{{ t.device_type_code }}</td>
              <td>{{ t.template_version }}</td>
            </tr>
          </tbody>
        </table>
        <p v-if="!templates.length" class="p-8 text-muted">暂无型号模板</p>
        <p class="p-4 text-[12px] text-muted">
          在设备详情右侧选择“型号模板”，可将模板补充应用到设备绑定。
        </p>
      </section></AsyncContent
    >
  </div>
</template>
