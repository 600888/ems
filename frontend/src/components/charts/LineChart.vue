<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ChartSeries } from '@/types/ui'

const props = withDefaults(
  defineProps<{
    series: ChartSeries[]
    /** 纵轴刻度，自上而下，首项为最大值 */
    yTicks: number[]
    /** 纵轴刻度的显示格式 */
    formatYTick?: (value: number) => string
    /** 横轴时间标签 */
    xLabels?: string[]
    /** 查询的完整时间范围，保留首尾无样本的区域。 */
    timeRange?: [number, number]
    /** 查询采样间隔；为 0 时只标记原始样本。 */
    sampleIntervalMs?: number
    /** 是否绘制水平网格 */
    grid?: boolean
    /** 提供时间格式化函数时，启用按实际采样时间显示的悬停提示 */
    formatHoverTime?: (timestamp: number) => string
    /** 为按时间排序的样本启用横轴缩放及动态刻度 */
    formatAxisTime?: (timestamp: number, span: number) => string
  }>(),
  { grid: true },
)

const yMax = computed(() => props.yTicks[0] ?? 100)
const yMin = computed(() => props.yTicks[props.yTicks.length - 1] ?? 0)
const zoomRange = ref<[number, number] | null>(null)
const timeBounds = computed<[number, number]>(() => {
  if (props.timeRange) return props.timeRange
  const times = props.series.flatMap((item) => item.timestamps ?? [])
  return times.length ? [Math.min(...times), Math.max(...times)] : [0, 0]
})
const visibleTimeBounds = computed(() => zoomRange.value ?? timeBounds.value)
const axisLabels = computed(() => {
  const format = props.formatAxisTime
  if (!format) return props.xLabels ?? []
  const [start, end] = visibleTimeBounds.value
  const span = end - start
  if (!span) return [format(start, span)]
  return Array.from({ length: 6 }, (_, i) => format(start + (span * i) / 5, span))
})

function firstAtOrAfter<T>(items: T[], time: number, timestamp: (item: T) => number) {
  let left = 0
  let right = items.length
  while (left < right) {
    const middle = Math.floor((left + right) / 2)
    if (timestamp(items[middle]!) < time) left = middle + 1
    else right = middle
  }
  return left
}

function timeX(timestamp: number) {
  const [start, end] = visibleTimeBounds.value
  return end > start ? (timestamp - start) / (end - start) : 0
}

/** 归一化到 0–100 的 viewBox 高度，配合 preserveAspectRatio=none 精确铺满 */
function toY(value: number) {
  const span = yMax.value - yMin.value || 1
  return 100 - ((value - yMin.value) / span) * 100
}

const paths = computed(() =>
  props.series.map((item) => {
    const count = item.values.length
    const timestamps = item.timestamps
    const connect = props.sampleIntervalMs !== 0
    let first = 0
    let last = count
    if (zoomRange.value && timestamps) {
      const [start, end] = zoomRange.value
      // 保留窗口两侧相邻的样本，曲线在绘图区边界裁切。
      first = Math.max(0, firstAtOrAfter(timestamps, start, (time) => time) - 1)
      last = Math.min(count, firstAtOrAfter(timestamps, Math.floor(end) + 1, (time) => time) + 1)
    }
    let d = ''
    let open = false
    for (let index = first; index < last; index++) {
      const value = item.values[index]
      if (value == null) {
        open = false
        continue
      }
      const timestamp = timestamps?.[index]
      const x = timestamp !== undefined ? timeX(timestamp) : count > 1 ? index / (count - 1) : 0
      const coords = `${(x * 1000).toFixed(2)},${toY(value).toFixed(2)}`
      d += `${open && connect ? 'L' : 'M'}${coords} `
      if (!connect || (props.formatHoverTime && !open && item.values[index + 1] == null))
        d += `L${coords} `
      open = true
    }
    return { ...item, d: d.trim() }
  }),
)

const gridYs = computed(() => props.yTicks.map((tick) => toY(tick)))

const pointer = ref<{ fraction: number; width: number } | null>(null)
const hoverSamples = computed(() => {
  type Reading = { name: string; color: string; value: number }
  const samples = new Map<number, Reading[]>()
  if (props.formatHoverTime) {
    for (const { name, color, timestamps, values } of props.series) {
      timestamps?.forEach((timestamp, index) => {
        const value = values[index]
        if (value == null || !Number.isFinite(value)) return
        const reading = { name, color, value }
        const readings = samples.get(timestamp)
        if (readings) readings.push(reading)
        else samples.set(timestamp, [reading])
      })
    }
  }
  return [...samples].sort(([a], [b]) => a - b)
})

const hovered = computed(() => {
  const samples = hoverSamples.value
  if (!pointer.value || !samples.length) return null
  const [start, end] = visibleTimeBounds.value
  const target = start + pointer.value.fraction * (end - start)
  // 只在附近有实际样本时显示提示，且不跨过按查询起点对齐的空桶。
  const radius = ((end - start) * 8) / pointer.value.width
  let firstTime = Math.max(start, target - radius)
  let afterTime = Math.floor(Math.min(end, target + radius)) + 1
  const interval = props.sampleIntervalMs
  if (interval && props.timeRange) {
    const origin = props.timeRange[0]
    const bucket = origin + Math.floor((target - origin) / interval) * interval
    firstTime = Math.max(firstTime, bucket)
    afterTime = Math.min(afterTime, bucket + interval)
  }
  const timestampOf = (sample: (typeof samples)[number]) => sample[0]
  const first = firstAtOrAfter(samples, firstTime, timestampOf)
  const last = firstAtOrAfter(samples, afterTime, timestampOf)
  if (first >= last) return null
  const index = Math.min(last - 1, firstAtOrAfter(samples, target, timestampOf))
  const current = samples[index]!
  const previous = index > first ? samples[index - 1] : undefined
  const [timestamp, readings] =
    previous && target - previous[0] <= Math.abs(current[0] - target) ? previous : current
  return { timestamp, readings, x: timeX(timestamp) * 100 }
})

function plotPosition(event: MouseEvent) {
  const { left, width } = (event.currentTarget as HTMLElement).getBoundingClientRect()
  return width
    ? { width, fraction: Math.max(0, Math.min(1, (event.clientX - left) / width)) }
    : null
}

function movePointer(event: PointerEvent) {
  if (props.formatHoverTime) pointer.value = plotPosition(event)
}

function zoomTime(event: WheelEvent) {
  const [min, max] = timeBounds.value
  if (!props.formatAxisTime || max <= min || !event.deltaY || event.ctrlKey) return
  const position = plotPosition(event)
  if (!position) return
  event.preventDefault()
  pointer.value = position

  const [start, end] = visibleTimeBounds.value
  const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? 100 : 1)
  const scale = Math.exp(Math.max(-1, Math.min(1, delta / 500)))
  const span = Math.max(1, (end - start) * scale)
  if (span >= max - min) {
    zoomRange.value = null
  } else {
    const nextStart = Math.max(
      min,
      Math.min(max - span, start + position.fraction * (end - start - span)),
    )
    zoomRange.value = [nextStart, Math.min(max, nextStart + span)]
  }
}

watch(
  () => props.series,
  () => {
    pointer.value = null
    zoomRange.value = null
  },
)
</script>

<template>
  <div class="flex min-h-0 flex-1 gap-3">
    <!-- 纵轴刻度：36px，右对齐，与画板一致 -->
    <div
      class="flex w-9 shrink-0 flex-col justify-between pb-[22px] text-right text-caption text-subtle"
    >
      <span v-for="(tick, index) in yTicks" :key="index">
        {{ formatYTick ? formatYTick(tick) : tick }}
      </span>
    </div>

    <div class="flex min-w-0 flex-1 flex-col">
      <div
        class="relative min-h-0 flex-1"
        :class="{ 'cursor-pointer': formatHoverTime }"
        @pointermove="movePointer"
        @pointerleave="pointer = null"
        @pointercancel="pointer = null"
        @wheel="zoomTime"
      >
        <svg
          class="absolute inset-0 h-full w-full"
          :class="zoomRange ? 'overflow-hidden' : 'overflow-visible'"
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
            v-for="(item, index) in paths"
            :key="index"
            :d="item.d"
            fill="none"
            :stroke="item.color"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
            vector-effect="non-scaling-stroke"
          />
        </svg>
        <template v-if="hovered && formatHoverTime">
          <div
            class="pointer-events-none absolute inset-y-0 border-l border-dashed border-subtle"
            :style="{ left: `${hovered.x}%` }"
            aria-hidden="true"
          />
          <i
            v-for="(reading, index) in hovered.readings"
            :key="index"
            class="pointer-events-none absolute size-2 -translate-x-1/2 -translate-y-1/2 rounded-full border border-panel"
            :style="{
              left: `${hovered.x}%`,
              top: `${toY(reading.value)}%`,
              backgroundColor: reading.color,
            }"
            aria-hidden="true"
          />
          <div
            role="tooltip"
            class="pointer-events-none absolute top-2 z-10 max-w-[min(20rem,calc(50%-10px))] rounded-md border border-line bg-panel px-3 py-2 text-caption text-ink shadow-lg"
            :style="
              hovered.x <= 50
                ? { left: `calc(${hovered.x}% + 10px)` }
                : { right: `calc(${100 - hovered.x}% + 10px)` }
            "
          >
            <div class="mb-1 text-muted tabular-nums">{{ formatHoverTime(hovered.timestamp) }}</div>
            <div
              v-for="(reading, index) in hovered.readings"
              :key="index"
              class="flex items-center gap-2"
            >
              <i class="dot shrink-0" :style="{ backgroundColor: reading.color }" />
              <span class="min-w-0 wrap-break-word">{{ reading.name }}</span>
              <span class="ml-auto shrink-0 font-medium tabular-nums">{{ reading.value }}</span>
            </div>
          </div>
        </template>
      </div>

      <div class="flex shrink-0 justify-between pt-[6px] text-caption text-subtle tabular-nums">
        <span v-for="(label, index) in axisLabels" :key="index">{{ label }}</span>
      </div>
    </div>
  </div>
</template>
