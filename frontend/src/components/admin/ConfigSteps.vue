<script setup lang="ts">
import AppIcon from '@/components/common/AppIcon.vue'

const props = defineProps<{
  /** 当前步骤 1–4 */
  current: number
}>()

const steps = [
  { index: 1, label: '选择点表' },
  { index: 2, label: '预览与通道' },
  { index: 3, label: '映射固有点位' },
  { index: 4, label: '校验与发布' },
]
</script>

<template>
  <ol class="flex shrink-0 items-center gap-2">
    <template v-for="(step, i) in steps" :key="step.index">
      <li class="flex items-center gap-2">
        <span
          v-if="step.index < props.current"
          class="flex h-[18px] w-[18px] items-center justify-center rounded-full text-ok"
        >
          <AppIcon name="circle-check" :size="15" />
        </span>
        <span
          v-else
          class="flex h-[18px] w-[18px] items-center justify-center rounded-full border text-[10px] font-medium"
          :class="
            step.index === props.current
              ? 'border-accent bg-sel text-accent'
              : 'border-line text-subtle'
          "
        >
          {{ step.index }}
        </span>
        <span
          class="text-[12px]"
          :class="
            step.index === props.current
              ? 'font-semibold text-accent'
              : step.index < props.current
                ? 'text-muted'
                : 'text-subtle'
          "
        >
          {{ step.label }}
        </span>
      </li>
      <AppIcon v-if="i < steps.length - 1" name="chevron-right" :size="13" class="text-subtle" />
    </template>
  </ol>
</template>
