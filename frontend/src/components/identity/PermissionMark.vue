<script setup lang="ts">
import AppIcon from '@/components/common/AppIcon.vue'
defineProps<{ allowed: boolean }>()
</script>
<template>
  <span
    class="inline-flex shrink-0 items-center justify-center"
    :class="allowed ? 'text-ok' : 'text-danger'"
    role="img"
    :aria-label="allowed ? '允许' : '禁止'"
    ><AppIcon :name="allowed ? 'check' : 'x'" :size="21" :stroke-width="2"
  /></span>
</template>
