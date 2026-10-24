<script setup lang="ts">
import {computed} from 'vue'
import AppIcon from './AppIcon.vue'

/** 操作层级：主操作 / 次操作 / 不可用（画板 09） */
const props = withDefaults(
  defineProps<{
    variant?: 'primary' | 'secondary' | 'ghost' | 'danger'
    icon?: string
    /** md=38px，sm=34px */
    size?: 'md' | 'sm'
    disabled?: boolean
    /** 不可用原因，给出时以 tooltip 说明（设计规范要求禁用须说明原因） */
    disabledReason?: string
  }>(),
  {variant: 'secondary', size: 'md'},
)

const variantClass = computed(
  () =>
    ({
      primary: 'bg-brand text-white border border-brand hover:brightness-110',
      secondary: 'bg-raised text-ink border border-line hover:border-subtle',
      ghost: 'bg-transparent text-muted border border-transparent hover:bg-raised hover:text-ink',
      danger: 'bg-danger-soft text-danger border border-danger/40 hover:brightness-105',
    })[props.variant],
)

const sizeClass = computed(() => (props.size === 'md' ? 'h-[38px] px-[14px] text-[13px]' : 'h-[34px] px-3 text-[12px]'))
</script>

<template>
  <el-tooltip v-if="disabled && disabledReason" :content="disabledReason" placement="top" :show-after="120">
    <span class="inline-flex">
      <button
        type="button"
        class="inline-flex shrink-0 items-center justify-center gap-[7px] rounded-[5px] font-medium whitespace-nowrap transition-colors"
        :class="[variantClass, sizeClass, disabled && 'cursor-not-allowed opacity-50']"
        :disabled="disabled"
      >
        <AppIcon v-if="icon" :name="icon" :size="size === 'md' ? 15 : 14" />
        <slot />
      </button>
    </span>
  </el-tooltip>

  <button
    v-else
    type="button"
    class="inline-flex shrink-0 items-center justify-center gap-[7px] rounded-[5px] font-medium whitespace-nowrap transition-colors"
    :class="[variantClass, sizeClass, disabled && 'cursor-not-allowed opacity-50']"
    :disabled="disabled"
  >
    <AppIcon v-if="icon" :name="icon" :size="size === 'md' ? 15 : 14" />
    <slot />
  </button>
</template>
