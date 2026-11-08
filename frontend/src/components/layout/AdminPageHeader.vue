<script setup lang="ts">
import AppIcon from '@/components/common/AppIcon.vue'

defineProps<{ title: string; description: string; icon: string }>()
</script>

<template>
  <header class="admin-page-header shrink-0 border-b border-line bg-surface">
    <div class="flex min-w-0 items-center gap-3">
      <span
        class="flex h-10 w-10 shrink-0 items-center justify-center rounded-md border border-line bg-iconbox text-accent"
        aria-hidden="true"
      >
        <AppIcon :name="icon" :size="21" />
      </span>
      <div class="min-w-0">
        <h1 class="truncate text-title leading-tight font-semibold text-ink" :title="title">
          {{ title }}
        </h1>
        <p class="mt-1 truncate text-small text-muted" :title="description">{{ description }}</p>
      </div>
    </div>
    <div id="admin-page-actions" class="admin-page-actions" role="group" aria-label="页面操作" />
  </header>
</template>
