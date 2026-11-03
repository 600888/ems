<script setup lang="ts">
import { watch } from 'vue'
import { useMonitorStore } from '@/stores/monitor'
const monitor = useMonitorStore()
watch(
  () => monitor.pages.yx,
  () => {
    void monitor.refresh()
  },
)
</script>
<template>
  <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
    <div class="p-4 font-semibold">遥信 · {{ monitor.totals.yx }} 项</div>
    <div class="min-h-0 flex-1 overflow-x-auto">
      <table class="identity-table identity-table-compact">
        <thead>
          <tr>
            <th>信号名称</th>
            <th>当前值</th>
            <th>最近采集时间</th>
            <th>质量</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in monitor.yxList" :key="row.name">
            <td>{{ row.name }}</td>
            <td>{{ row.state }}</td>
            <td>{{ row.lastChange }}</td>
            <td>{{ row.level === 'normal' ? '有效' : '不可用' }}</td>
          </tr>
        </tbody>
      </table>
    </div>
    <el-pagination
      v-model:current-page="monitor.pages.yx"
      :page-size="200"
      :total="monitor.totals.yx"
      layout="prev, pager, next"
      class="p-4"
    />
  </section>
</template>
