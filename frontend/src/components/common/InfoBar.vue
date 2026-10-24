<script setup lang="ts">
import { computed } from 'vue'
import AppIcon from './AppIcon.vue'

const props = withDefaults(
  defineProps<{
    tone?: 'info' | 'ok' | 'warn' | 'danger'
    text: string
    /** 覆盖默认图标 */
    icon?: string
  }>(),
  { tone: 'info' },
)

const toneMap = {
  info: { cls: 'bg-info-soft text-accent', icon: 'info' },
  ok: { cls: 'bg-okbox text-ok', icon: 'info' },
  warn: { cls: 'bg-warnbox text-warn', icon: 'info' },
  danger: { cls: 'bg-danger-soft text-danger', icon: 'triangle-alert' },
}

const toneCls = computed(() => toneMap[props.tone].cls)
const iconName = computed(() => props.icon ?? toneMap[props.tone].icon)
</script>

<template>
  <div class="flex items-center gap-2 rounded-[4px] px-[14px] py-3" :class="toneCls">
    <AppIcon :name="iconName" :size="14" />
    <p class="text-[11px] leading-[1.5]">
      <slot>{{ text }}</slot>
    </p>
  </div>
</template>
