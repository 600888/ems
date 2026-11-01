<script setup lang="ts">
import { computed, onMounted } from 'vue'
import LineChart from '@/components/charts/LineChart.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { useMonitorStore } from '@/stores/monitor'
const monitor = useMonitorStore()
const times = computed(() =>
  [...new Set(monitor.history.map((s) => s.timestamp))].sort((a, b) => a - b),
)
const series = computed(() =>
  monitor.ycList
    .filter((p) => monitor.selectedYcCodes.includes(p.code))
    .map((p, i) => {
      const samples = monitor.history.filter((sample) => sample.point_id === p.pointId)
      return {
        name: p.name,
        color: ['#55d9ef', '#287bff', '#f5bc62', '#48d8ad', '#ff7883', '#a6a0ff'][i]!,
        timestamps: samples.map((sample) => sample.timestamp),
        values: samples.map((sample) =>
          sample.quality_code === 0 && typeof sample.value === 'number' ? sample.value : null,
        ),
      }
    }),
)
const ticks = computed(() => {
  const values = series.value
    .flatMap((s) => s.values)
    .filter((v): v is number => typeof v === 'number')
  const lo = values.length ? Math.min(...values) : 0,
    hi = values.length ? Math.max(...values) : 1,
    span = hi - lo || 1
  return [hi + span * 0.1, lo + span * 0.75, lo + span * 0.5, lo + span * 0.25, lo - span * 0.1]
})
const labels = computed(() =>
  (times.value.length
    ? Array.from(
        { length: 6 },
        (_, i) =>
          times.value[0]! + ((times.value[times.value.length - 1]! - times.value[0]!) * i) / 5,
      )
    : []
  ).map((t) => new Date(t).toLocaleTimeString('zh-CN', { hour12: false })),
)
function exportData() {
  const csv = [
    'point_id,timestamp,value,quality',
    ...monitor.history.map((s) =>
      [s.point_id, new Date(s.timestamp).toISOString(), s.value, s.quality_code]
        .map((v) => `"${String(v ?? '').replaceAll('"', '""')}"`)
        .join(','),
    ),
  ].join('\r\n')
  const url = URL.createObjectURL(new Blob(['\ufeff' + csv], { type: 'text/csv;charset=utf-8' }))
  const a = document.createElement('a')
  a.href = url
  a.download = '历史采样.csv'
  a.click()
  URL.revokeObjectURL(url)
}
onMounted(() => {
  void monitor.queryHistory()
})
</script>
<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <div class="panel flex shrink-0 flex-wrap items-center gap-3 p-4">
      <el-select v-model="monitor.curveWindow" class="w-[180px]!"
        ><el-option label="最近 30 分钟" value="最近 30 分钟" /><el-option
          label="最近 2 小时"
          value="最近 2 小时" /></el-select
      ><el-select
        v-model="monitor.selectedYcCodes"
        multiple
        :multiple-limit="6"
        placeholder="选择测点（最多 6 项）"
        class="min-w-[300px]!"
        ><el-option
          v-for="p in monitor.ycList"
          :key="p.code"
          :label="p.name"
          :value="p.code" /></el-select
      ><ActionBtn variant="primary" :loading="monitor.historyBusy" @click="monitor.queryHistory"
        >查询</ActionBtn
      ><ActionBtn :disabled="!monitor.history.length" @click="exportData">导出采样</ActionBtn>
    </div>
    <InfoBar v-if="monitor.historyError" :text="monitor.historyError" tone="danger" /><InfoBar
      v-if="monitor.historyTruncated"
      text="结果已达到 10000 条上限，请缩小查询范围。"
      tone="warn"
    />
    <section class="panel flex min-h-0 flex-1 flex-col p-5">
      <LineChart
        v-if="monitor.history.length"
        :series="series"
        :y-ticks="ticks"
        :x-labels="labels"
      />
      <div v-else class="flex flex-1 items-center justify-center text-muted">
        {{
          monitor.historyBusy ? '正在查询…' : '选择测点并查询历史采样；没有数据时不会生成示例曲线。'
        }}
      </div>
    </section>
  </div>
</template>
