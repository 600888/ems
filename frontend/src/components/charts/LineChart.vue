<script setup lang="ts">
import { computed } from 'vue'
import type { ChartSeries } from '@/types/ui'

const props = withDefaults(
  defineProps<{
    series: ChartSeries[]
    /** 纵轴刻度，自上而下，首项为最大值 */
    yTicks: number[]
    /** 横轴时间标签 */
    xLabels: string[]
    /** 是否绘制水平网格 */
    grid?: boolean
  }>(),
  { grid: true },
)

const yMax = computed(() => props.yTicks[0] ?? 100)
const yMin = computed(() => props.yTicks[props.yTicks.length - 1] ?? 0)

/** 归一化到 0–100 的 viewBox 高度，配合 preserveAspectRatio=none 精确铺满 */
function toY(value: number) {
  const span = yMax.value - yMin.value || 1
  return 100 - ((value - yMin.value) / span) * 100
}

const paths = computed(() =>
  props.series.map((item) => {
    const count = item.values.length
    let d = ''
    let open = false
    item.values.forEach((value, index) => {
      if (value === null) {
        open = false
        return
      }
      const x = count > 1 ? (index / (count - 1)) * 1000 : 0
      d += `${open ? 'L' : 'M'}${x.toFixed(2)},${toY(value).toFixed(2)} `
      open = true
    })
    return { ...item, d: d.trim() }
  }),
)

const gridYs = computed(() => props.yTicks.map((tick) => toY(tick)))
</script>

<template>
  <div class="flex min-h-0 flex-1 gap-3">
    <!-- 纵轴刻度：36px，右对齐，与画板一致 -->
    <div
      class="flex w-9 shrink-0 flex-col justify-between pb-[22px] text-right text-[10px] text-subtle"
    >
      <span v-for="tick in yTicks" :key="tick">{{ tick }}</span>
    </div>

    <div class="flex min-w-0 flex-1 flex-col">
      <div class="relative min-h-0 flex-1">
        <svg
          class="absolute inset-0 h-full w-full overflow-visible"
          viewBox="0 0 1000 100"
          preserveAspectRatio="none"
          aria-hidden="true"
        >
          <template v-if="grid">
            <line
              v-for="(y, index) in gridYs"
              :key="index"
              x1="0"
              :y1="y"
              x2="1000"
              :y2="y"
              :stroke="'var(--c-line)'"
              stroke-width="1"
              vector-effect="non-scaling-stroke"
            />
          </template>
          <path
            v-for="item in paths"
            :key="item.name"
            :d="item.d"
            fill="none"
            :stroke="item.color"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
            vector-effect="non-scaling-stroke"
          />
        </svg>
      </div>

      <div class="flex shrink-0 justify-between pt-[6px] text-[10px] text-subtle">
        <span v-for="label in xLabels" :key="label">{{ label }}</span>
      </div>
    </div>
  </div>
</template>
