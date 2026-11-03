<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute } from 'vue-router'
import { request } from '@/api/http'
import { errorMessage } from '@/api/http'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
const route = useRoute(),
  audit = computed(() => route.name === 'audit'),
  busy = ref(false),
  error = ref(''),
  page = ref(1),
  more = ref(false),
  rows = ref<Record<string, unknown>[]>([]),
  metrics = ref<Record<string, unknown>>({})
async function load() {
  busy.value = true
  error.value = ''
  try {
    if (audit.value) {
      const result = await request<{ items: Record<string, unknown>[]; has_more: boolean }>(
        '/api/audit/list',
        { page: page.value, page_size: 20 },
      )
      rows.value = result.items
      more.value = result.has_more
    } else metrics.value = await request('/api/system/metrics')
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
    <div class="flex shrink-0 items-center justify-end">
      <ActionBtn :loading="busy" @click="load">刷新</ActionBtn>
    </div>
    <AsyncContent :loading="busy" :error="error" @retry="load"
      ><section v-if="audit" class="panel flex min-h-0 flex-1 flex-col">
        <div class="min-h-0 flex-1 overflow-x-auto">
          <table class="identity-table">
            <thead>
              <tr>
                <th>时间 UTC</th>
                <th>操作人</th>
                <th>动作</th>
                <th>目标</th>
                <th>结果</th>
                <th>原因</th>
                <th>请求 ID</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in rows" :key="String(row.event_id)">
                <td>{{ row.occurred_at }}</td>
                <td>{{ row.actor }}</td>
                <td>{{ row.action }}</td>
                <td>{{ row.target || '—' }}</td>
                <td>{{ row.outcome }}</td>
                <td>{{ row.reason || '—' }}</td>
                <td class="font-mono text-small">{{ row.request_id }}</td>
              </tr>
            </tbody>
          </table>
        </div>
        <div class="flex items-center justify-end gap-4 p-4">
          <ActionBtn
            :disabled="page === 1"
            @click="
              () => {
                page--
                load()
              }
            "
            >上一页</ActionBtn
          ><span class="text-body text-muted">第 {{ page }} 页</span
          ><ActionBtn
            :disabled="!more"
            @click="
              () => {
                page++
                load()
              }
            "
            >下一页</ActionBtn
          >
        </div>
      </section>
      <section v-else class="panel overflow-x-auto p-6">
        <h2 class="mb-5 text-section font-semibold">Web 服务指标</h2>
        <dl class="grid gap-4 md:grid-cols-2">
          <div v-for="(value, key) in metrics" :key="key" class="border-b border-line py-3">
            <dt class="text-body text-muted">{{ key }}</dt>
            <dd class="mt-2 font-mono text-body">
              {{ typeof value === 'object' ? JSON.stringify(value) : value }}
            </dd>
          </div>
        </dl>
      </section></AsyncContent
    >
  </div>
</template>
