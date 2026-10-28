<script setup lang="ts">
import { useId } from 'vue'
defineProps<{ label: string; hint?: string; error?: string; required?: boolean }>()
const id = useId()
</script>

<template>
  <div class="flex min-w-0 flex-col gap-2">
    <label :for="id" class="text-[14px] text-muted"
      >{{ label }}<span v-if="required" class="ml-1 text-danger" aria-hidden="true">*</span></label
    >
    <slot :id="id" :invalid="Boolean(error)" :description-id="`${id}-help`" />
    <p
      v-if="error || hint"
      :id="`${id}-help`"
      class="text-[12px] leading-relaxed"
      :class="error ? 'text-danger' : 'text-subtle'"
      :role="error ? 'alert' : undefined"
    >
      {{ error || hint }}
    </p>
  </div>
</template>
