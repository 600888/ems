<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { useMonitorStore } from '@/stores/monitor'
import ActionBtn from '@/components/common/ActionBtn.vue'
const monitor = useMonitorStore(),
  router = useRouter()
const keyTelemetry = computed(() => monitor.ycList.slice(0, 12))
</script>
<template>
  <div class="grid min-h-0 flex-1 gap-4 xl:grid-cols-2">
    <section class="panel min-h-0 overflow-auto">
      <div class="flex items-center justify-between p-4">
        <h3 class="font-semibold">遥测</h3>
        <ActionBtn @click="router.push('/monitor/telemetry')"
          >全部 {{ monitor.totals.yc }} 项</ActionBtn
        >
      </div>
      <table class="identity-table min-w-0!">
        <thead>
          <tr>
            <th>测点</th>
            <th>值</th>
            <th>单位</th>
            <th>质量</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="p in keyTelemetry" :key="p.code">
            <td>{{ p.name }}</td>
            <td class="text-accent">{{ p.value }}</td>
            <td>{{ p.unit }}</td>
            <td>{{ p.quality === 'good' ? '有效' : '不可用' }}</td>
          </tr>
        </tbody>
      </table>
      <p v-if="!keyTelemetry.length" class="p-8 text-muted">暂无遥测点位</p>
    </section>
    <section class="panel min-h-0 overflow-auto">
      <div class="flex items-center justify-between p-4">
        <h3 class="font-semibold">遥信</h3>
        <ActionBtn @click="router.push('/monitor/signals')"
          >全部 {{ monitor.totals.yx }} 项</ActionBtn
        >
      </div>
      <table class="identity-table min-w-0!">
        <thead>
          <tr>
            <th>信号</th>
            <th>当前值</th>
            <th>更新时间</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="p in monitor.yxList" :key="p.name">
            <td>{{ p.name }}</td>
            <td>{{ p.state }}</td>
            <td>{{ p.lastChange }}</td>
          </tr>
        </tbody>
      </table>
      <p v-if="!monitor.yxList.length" class="p-8 text-muted">暂无遥信点位</p>
    </section>
  </div>
</template>
