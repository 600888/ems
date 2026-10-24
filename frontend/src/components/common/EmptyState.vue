<script setup lang="ts">
import AppIcon from './AppIcon.vue'

withDefaults(
  defineProps<{
    icon?: string
    title: string
    desc?: string
    /** 容器最小高度，设计图为 194 */
    minHeight?: number
    tone?: 'muted' | 'warn' | 'danger'
  }>(),
  {icon: 'hard-drive', minHeight: 194, tone: 'muted'},
)
</script>

<template>
  <div
    class="flex flex-col items-center justify-center gap-[10px] rounded-[6px] border border-line bg-surface px-4 py-4 text-center"
    :style="{minHeight: `${minHeight}px`}"
  >
    <AppIcon
      :name="icon"
      :size="26"
      :class="tone === 'warn' ? 'text-warn' : tone === 'danger' ? 'text-danger' : 'text-subtle'"
    />
    <p class="text-[14px] font-medium text-ink">{{ title }}</p>
    <p v-if="desc" class="max-w-[220px] text-[11px] leading-[1.6] text-muted">{{ desc }}</p>
    <slot />
  </div>
</template>
