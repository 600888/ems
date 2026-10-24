<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import LineChart from '@/components/charts/LineChart.vue'
import { keyTelemetry, signalSummary } from '@/mocks/points'
import { useMonitorStore } from '@/stores/monitor'

const router = useRouter()
const monitor = useMonitorStore()

const socPoint = computed(() => monitor.ycList.find((point) => point.code === 'soc'))
const socSeries = computed(() => [
  {
    name: 'SOC',
    color: 'var(--c-accent)',
    values: socPoint.value?.series ?? [],
  },
])

const yTicks = [85, 80, 75, 70, 65]
const xLabels = ['14:02', '14:07', '14:12', '14:17', '14:22', '14:27', '14:32']
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <!-- 实时数据与设备状态 -->
    <div class="flex h-[340px] shrink-0 gap-4">
      <section class="panel flex min-w-0 flex-1 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">遥测 · 关键运行数据</h3>
          <button
            type="button"
            class="link-action text-[11px]"
            @click="router.push('/monitor/telemetry')"
          >
            全部 {{ monitor.profile.tabCounts.telemetry }} 个测点 →
          </button>
        </div>

        <div class="flex h-[30px] shrink-0 items-center bg-raised px-4">
          <span class="dt-th min-w-0 flex-1">测点名称</span>
          <span class="dt-th w-[136px] shrink-0">实时值</span>
          <span class="dt-th w-[70px] shrink-0">单位</span>
          <span class="dt-th w-[150px] shrink-0">极值位置</span>
          <span class="dt-th w-[70px] shrink-0">质量</span>
        </div>

        <div class="min-h-0 flex-1 overflow-y-auto">
          <div v-for="row in keyTelemetry" :key="row.name" class="dt-row h-[33px]">
            <span class="dt-cell min-w-0 flex-1 truncate text-[12px] text-ink">{{ row.name }}</span>
            <span class="dt-cell w-[136px] shrink-0 text-[12px] text-accent">
              {{ row.quality === 'timeout' ? '—' : row.value }}
            </span>
            <span class="dt-cell w-[70px] shrink-0 text-[11px] text-muted">{{ row.unit }}</span>
            <span class="dt-cell w-[150px] shrink-0 truncate text-[10px] text-muted">{{
              row.position
            }}</span>
            <span
              class="dt-cell w-[70px] shrink-0 text-[10px]"
              :class="row.quality === 'good' ? 'text-ok' : 'text-warn'"
            >
              {{ row.quality === 'good' ? '有效' : '超时' }}
            </span>
          </div>
        </div>
      </section>

      <section class="panel flex w-[446px] shrink-0 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">遥信 · 设备状态</h3>
          <button
            type="button"
            class="link-action text-[11px]"
            @click="router.push('/monitor/signals')"
          >
            全部 {{ monitor.profile.tabCounts.signal }} 个信号 →
          </button>
        </div>

        <div class="flex min-h-0 flex-1 flex-col gap-[10px] overflow-y-auto px-4 py-3">
          <div
            v-for="row in signalSummary"
            :key="row.label"
            class="flex h-[22px] items-center gap-[10px]"
          >
            <span class="text-[12px] text-muted">{{ row.label }}</span>
            <div class="flex-1" />
            <i
              class="dot"
              :class="
                row.tone === 'warn'
                  ? 'text-warn'
                  : row.tone === 'accent'
                    ? 'text-accent'
                    : 'text-ok'
              "
            />
            <span
              class="text-[12px]"
              :class="
                row.tone === 'warn'
                  ? 'text-warn'
                  : row.tone === 'accent'
                    ? 'text-accent'
                    : 'text-ok'
              "
            >
              {{ row.value }}
            </span>
          </div>

          <div class="flex flex-col gap-[7px] rounded-[4px] bg-warnbox px-3 py-[10px]">
            <div class="flex items-center gap-[10px]">
              <AppIcon name="triangle-alert" :size="13" class="text-warn" />
              <span class="text-[11px] font-medium text-warn">电池簇 02 · 单体温差预警</span>
            </div>
            <span class="text-[10px] text-warn">14:28:36 触发 · 5.4 °C / 阈值 5.0 °C</span>
          </div>
        </div>
      </section>
    </div>

    <!-- SOC 实时趋势 -->
    <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
      <div class="panel-bar">
        <h3 class="text-[14px] font-semibold text-ink">实时曲线 · SOC</h3>
        <div class="flex items-center gap-[14px]">
          <div class="flex items-center gap-[6px]">
            <span class="h-[2px] w-4 bg-accent" />
            <span class="text-[11px] text-accent">SOC {{ socPoint?.value ?? '—' }} %</span>
          </div>
          <span class="text-[11px] text-muted">最近 30 分钟</span>
          <button
            type="button"
            class="flex h-[34px] items-center gap-[6px] rounded-[5px] border border-line bg-raised px-3 text-[12px] font-medium text-ink transition-colors hover:border-subtle"
            @click="router.push('/monitor/curves')"
          >
            <AppIcon name="maximize-2" :size="14" />
            展开曲线
          </button>
        </div>
      </div>

      <div class="flex min-h-0 flex-1 px-[14px] py-3">
        <LineChart :series="socSeries" :y-ticks="yTicks" :x-labels="xLabels" />
      </div>
    </section>
  </div>
</template>
