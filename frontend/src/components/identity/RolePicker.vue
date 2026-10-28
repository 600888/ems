<script setup lang="ts">
import { roles } from '@/domain/identity'
import type { RoleCode } from '@/types/identity'
defineProps<{ modelValue: RoleCode; disabled?: boolean }>()
const emit = defineEmits<{ 'update:modelValue': [value: RoleCode] }>()
</script>
<template>
  <div class="grid gap-3 sm:grid-cols-3" role="radiogroup" aria-label="用户角色">
    <button
      v-for="role in roles"
      :key="role.code"
      type="button"
      role="radio"
      :aria-checked="modelValue === role.code"
      :disabled="disabled"
      class="flex items-center gap-3 rounded-[5px] border px-4 py-[18px] text-left disabled:cursor-not-allowed disabled:opacity-60"
      :class="
        modelValue === role.code
          ? 'border-accent bg-info-soft'
          : 'border-line bg-surface hover:border-subtle'
      "
      @click="emit('update:modelValue', role.code)"
    >
      <span
        class="flex h-[18px] w-[18px] shrink-0 items-center justify-center rounded-full border"
        :class="modelValue === role.code ? 'border-accent' : 'border-subtle'"
        ><span v-if="modelValue === role.code" class="h-2 w-2 rounded-full bg-accent"
      /></span>
      <span class="flex flex-col gap-1"
        ><span
          class="text-[15px] font-medium"
          :class="modelValue === role.code ? 'text-accent' : 'text-ink'"
          >{{ role.label }}</span
        ><span class="text-[12px] text-muted">{{ role.description }}</span></span
      >
    </button>
  </div>
</template>
