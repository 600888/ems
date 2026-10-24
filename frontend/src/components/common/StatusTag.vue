<script setup lang="ts">
import { computed } from 'vue'

/** 状态标签色调，对应画板 09「运行状态」色板 */
export type TagTone = 'ok' | 'warn' | 'danger' | 'mute' | 'accent' | 'info'

const props = withDefaults(
  defineProps<{
    tone?: TagTone
    label: string
    /** 是否显示状态点 */
    dot?: boolean
  }>(),
  { tone: 'ok', dot: true },
)

const toneClass = computed(
  () =>
    ({
      ok: 'bg-ok-soft text-ok',
      warn: 'bg-warn-soft text-warn',
      danger: 'bg-danger-soft text-danger',
      mute: 'bg-mute-soft text-muted',
      accent: 'bg-sel text-accent',
      info: 'bg-info-soft text-accent',
    })[props.tone],
)
</script>

<template>
  <span
    class="inline-flex shrink-0 items-center gap-[5px] rounded-[4px] px-2 py-1 text-[11px] leading-none font-medium"
    :class="toneClass"
  >
    <i v-if="dot" class="dot" />
    {{ label }}
  </span>
</template>
