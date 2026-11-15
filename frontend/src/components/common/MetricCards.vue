<script setup lang="ts">
import AppIcon from './AppIcon.vue'

export interface MetricCard {
  label: string
  value: string | number
  unit?: string
  note?: string
  icon?: string
  tone?: 'accent' | 'ink' | 'ok' | 'warn' | 'danger'
}
withDefaults(defineProps<{ items: MetricCard[]; inline?: boolean }>(), { inline: false })
const tones = {
  accent: 'text-accent',
  ink: 'text-ink',
  ok: 'text-ok',
  warn: 'text-warn',
  danger: 'text-danger',
}
</script>

<template>
  <div
    class="grid shrink-0 gap-3"
    :class="inline ? 'grid-cols-2 xl:grid-cols-5' : 'grid-cols-2 md:flex'"
  >
    <article
      v-for="metric in items"
      :key="metric.label"
      class="panel flex min-w-0 flex-1 gap-3 px-4 py-3"
      :class="inline ? 'items-center' : 'flex-col justify-center gap-[5px]!'"
    >
      <AppIcon
        v-if="metric.icon"
        :name="metric.icon"
        :size="22"
        :class="tones[metric.tone ?? 'ink']"
      />
      <div class="flex min-w-0 flex-col gap-[5px]">
        <span v-if="!inline" class="truncate text-small leading-none text-muted">{{
          metric.label
        }}</span>
        <div class="flex items-baseline gap-2">
          <span
            class="truncate text-metric leading-none font-semibold tabular-nums"
            :class="tones[metric.tone ?? 'ink']"
            >{{ metric.value }}</span
          >
          <span v-if="inline || metric.unit" class="text-small text-muted">{{
            inline ? metric.label : metric.unit
          }}</span>
        </div>
        <span v-if="metric.note" class="truncate text-caption text-subtle" :title="metric.note">{{
          metric.note
        }}</span>
      </div>
    </article>
  </div>
</template>
