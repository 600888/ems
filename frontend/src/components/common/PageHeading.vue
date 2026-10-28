<script setup lang="ts">
defineProps<{ title: string; description?: string; breadcrumb?: string }>()
</script>

<template>
  <div class="flex shrink-0 flex-col gap-6">
    <p v-if="breadcrumb" class="text-[12px] text-subtle">{{ breadcrumb }}</p>
    <div class="flex flex-wrap items-center justify-between gap-4">
      <div class="min-w-0">
        <h1 class="text-[26px] leading-snug font-semibold text-ink">{{ title }}</h1>
        <p v-if="description" class="mt-1.5 text-[14px] leading-relaxed text-muted">
          {{ description }}
        </p>
      </div>
      <div v-if="$slots.actions" class="flex flex-wrap items-center gap-3">
        <slot name="actions" />
      </div>
    </div>
  </div>
</template>
