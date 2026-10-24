<script setup lang="ts">
import {computed} from 'vue'
import {ElMessage} from 'element-plus'
import {useRouter} from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import LineChart from '@/components/charts/LineChart.vue'
import {useMonitorStore} from '@/stores/monitor'
import type {ChartSeries} from '@/types/ui'

const router = useRouter()
const monitor = useMonitorStore()

const xLabels = ['14:02', '14:07', '14:12', '14:17', '14:22', '14:27', '14:32']

const socPoint = computed(() => monitor.ycList.find((point) => point.code === 'soc'))
const tempMax = computed(() => monitor.ycList.find((point) => point.code === 'cell_temp_max'))
const tempMin = computed(() => monitor.ycList.find((point) => point.code === 'cell_temp_min'))

const socSeries = computed<ChartSeries[]>(() => [
  {name: 'SOC', color: 'var(--c-accent)', values: socPoint.value?.series ?? []},
])

const tempSeries = computed<ChartSeries[]>(() => [
  {name: '最高单体温度', color: 'var(--c-accent)', values: tempMax.value?.series ?? []},
  {name: '最低单体温度', color: 'var(--c-brand)', values: tempMin.value?.series ?? []},
])

function stats(values: number[] | undefined) {
  if (!values || !values.length) return {min: '—', max: '—', avg: '—', last: '—'}
  const min = Math.min(...values)
  const max = Math.max(...values)
  const avg = values.reduce((sum, value) => sum + value, 0) / values.length
  return {min: min.toFixed(1), max: max.toFixed(1), avg: avg.toFixed(1), last: values[values.length - 1].toFixed(1)}
}

const socStats = computed(() => stats(socPoint.value?.series))
const tempStats = computed(() => ({...stats(tempMax.value?.series), min: stats(tempMin.value?.series).last}))

const chips = computed(() =>
  monitor.selectedYcCodes
    .map((code) => monitor.ycList.find((point) => point.code === code))
    .filter((point): point is NonNullable<typeof point> => Boolean(point)),
)

function removePoint(code: string) {
  monitor.toggleYc(code)
}
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <!-- 工具栏 -->
    <div class="panel flex h-[46px] shrink-0 items-center gap-3 px-4">
      <el-select v-model="monitor.curveWindow" class="ctl-sm w-[132px]!">
        <el-option label="最近 30 分钟" value="最近 30 分钟" />
        <el-option label="最近 2 小时" value="最近 2 小时" />
        <el-option label="最近 24 小时" value="最近 24 小时" />
      </el-select>

      <el-select v-model="monitor.sampleInterval" class="ctl-sm w-[132px]!">
        <el-option label="采样间隔 2 秒" value="2 秒" />
        <el-option label="采样间隔 10 秒" value="10 秒" />
        <el-option label="采样间隔 1 分钟" value="1 分钟" />
      </el-select>

      <label class="flex items-center gap-2 text-[11px] text-muted">
        <el-switch v-model="monitor.paused" size="small" />
        暂停刷新
      </label>

      <div class="flex-1" />

      <span class="flex items-center gap-[6px] text-[11px]">
        <i class="dot" :class="monitor.paused ? 'text-warn' : 'text-ok'" />
        <span :class="monitor.paused ? 'text-warn' : 'text-ok'">{{ monitor.paused ? '已暂停' : '实时采样中' }}</span>
      </span>
      <span class="text-[11px] text-muted">14:02:08 – {{ monitor.lastUpdated }}</span>

      <button
        type="button"
        class="flex h-[34px] items-center gap-[6px] rounded-[5px] border border-line bg-raised px-3 text-[12px] font-medium text-ink transition-colors hover:border-subtle"
        @click="ElMessage.success('已导出当前曲线采样数据（示例）')"
      >
        <AppIcon name="download" :size="14" />
        导出数据
      </button>
    </div>

    <!-- 已选测点 -->
    <div class="flex h-8 shrink-0 items-center gap-2">
      <span class="text-[11px] text-muted">对比测点</span>
      <span
        v-for="point in chips"
        :key="point.code"
        class="flex items-center gap-[6px] rounded-[4px] bg-sel px-[9px] py-1 text-[11px] text-accent"
      >
        {{ point.name }}
        <button type="button" class="text-accent/70 hover:text-accent" @click="removePoint(point.code)">
          <AppIcon name="x" :size="11" />
        </button>
      </span>
      <button
        type="button"
        class="flex items-center gap-1 rounded-[4px] border border-dashed border-line px-[9px] py-1 text-[11px] text-muted hover:text-ink"
        @click="router.push('/monitor/telemetry')"
      >
        添加测点 {{ chips.length }} / 6
      </button>
      <div class="flex-1" />
      <span class="text-[10px] text-subtle">不同单位分图显示</span>
    </div>

    <!-- SOC 趋势 -->
    <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
      <div class="panel-bar">
        <h3 class="text-[14px] font-semibold text-ink">SOC · %</h3>
        <div class="flex items-center gap-[14px] text-[11px] text-muted">
          <span>最新 <span class="text-accent">{{ socStats.last }}</span></span>
          <span>最小 <span class="text-ink">{{ socStats.min }}</span></span>
          <span>最大 <span class="text-ink">{{ socStats.max }}</span></span>
          <span>平均 <span class="text-ink">{{ socStats.avg }}</span></span>
        </div>
      </div>
      <div class="flex min-h-0 flex-1 px-[14px] py-3">
        <LineChart :series="socSeries" :y-ticks="[85, 80, 75, 70, 65]" :x-labels="xLabels" />
      </div>
    </section>

    <!-- 单体温度趋势 -->
    <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
      <div class="panel-bar">
        <h3 class="text-[14px] font-semibold text-ink">单体温度 · °C</h3>
        <div class="flex items-center gap-[14px] text-[11px] text-muted">
          <span class="flex items-center gap-[6px]">
            <span class="h-[2px] w-3 bg-accent" />最高 {{ tempStats.max }}
          </span>
          <span class="flex items-center gap-[6px]">
            <span class="h-[2px] w-3 bg-brand" />最低 {{ tempStats.min }}
          </span>
          <span>当前温差 <span class="text-warn">5.4 °C</span></span>
        </div>
      </div>
      <div class="flex min-h-0 flex-1 px-[14px] py-3">
        <LineChart :series="tempSeries" :y-ticks="[40, 35, 30, 25]" :x-labels="xLabels" />
      </div>
    </section>
  </div>
</template>
