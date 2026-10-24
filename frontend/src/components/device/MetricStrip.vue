<script setup lang="ts">
import { useMonitorStore } from '@/stores/monitor'

const monitor = useMonitorStore()

const toneClass = {
  accent: 'text-accent',
  ink: 'text-ink',
  ok: 'text-ok',
  warn: 'text-warn',
  danger: 'text-danger',
}
</script>

<template>
  <div class="flex h-24 shrink-0 gap-3">
    <article
      v-for="metric in monitor.profile.metrics"
      :key="metric.label"
      class="flex min-w-0 flex-1 flex-col justify-center gap-[5px] rounded-[6px] border border-line bg-panel px-4 py-3"
    >
      <span class="truncate text-[11px] text-muted">{{ metric.label }}</span>
      <div class="flex items-baseline gap-[6px]">
        <span class="text-[25px] leading-none font-semibold" :class="toneClass[metric.tone]">
          {{ metric.value }}
        </span>
        <span v-if="metric.unit" class="text-[11px] text-muted">{{ metric.unit }}</span>
      </div>
      <span
        class="truncate text-[10px]"
        :class="metric.tone === 'warn' ? 'text-warn' : 'text-subtle'"
      >
        {{ metric.note }}
      </span>
    </article>
  </div>
</template>
