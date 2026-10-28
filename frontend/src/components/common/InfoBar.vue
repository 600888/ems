<script setup lang="ts">
import { computed } from 'vue'
import AppIcon from './AppIcon.vue'

const props = withDefaults(
  defineProps<{
    tone?: 'info' | 'ok' | 'warn' | 'danger'
    text: string
    title?: string
    size?: 'sm' | 'md'
    /** 覆盖默认图标 */
    icon?: string
  }>(),
  { tone: 'info', size: 'sm' },
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
  <div
    class="flex gap-3 rounded-[5px] px-[14px] py-3"
    :class="[
      toneCls,
      size === 'md' ? 'items-start border border-current/20 p-[16px]!' : 'items-center',
    ]"
    role="status"
  >
    <AppIcon :name="iconName" :size="size === 'md' ? 18 : 14" class="mt-0.5" />
    <div class="min-w-0 flex-1">
      <p v-if="title" class="mb-1 text-[14px] font-medium">{{ title }}</p>
      <p
        :class="
          size === 'md' ? 'text-[13px] leading-relaxed text-muted' : 'text-[11px] leading-[1.5]'
        "
      >
        <slot>{{ text }}</slot>
      </p>
    </div>
  </div>
</template>
