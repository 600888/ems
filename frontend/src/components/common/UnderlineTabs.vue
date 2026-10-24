<script setup lang="ts">
import type {TabItem} from '@/types/ui'

defineProps<{
  items: TabItem[]
  modelValue: string
  /** 标签栏高度：后台分栏 44，前台数据页签 42 */
  height?: number
}>()

const emit = defineEmits<{ 'update:modelValue': [key: string] }>()
</script>

<template>
  <div
    class="flex shrink-0 items-stretch gap-[26px] border-b border-line px-4"
    :style="{height: `${height ?? 44}px`}"
  >
    <button
      v-for="item in items"
      :key="item.key"
      type="button"
      class="relative flex items-center gap-[6px] px-[3px] text-[12px] transition-colors"
      :class="item.key === modelValue ? 'font-semibold text-accent' : 'text-muted hover:text-ink'"
      @click="emit('update:modelValue', item.key)"
    >
      {{ item.label }}
      <span v-if="item.count !== undefined" class="text-[10px] font-normal text-subtle">{{ item.count }}</span>
      <span v-if="item.key === modelValue" class="absolute -bottom-px left-0 right-0 h-[2px] bg-accent" />
    </button>
  </div>
</template>
