<script setup lang="ts">
import { roleLabel } from '@/domain/identity'
import type { RoleCode } from '@/types/identity'
defineProps<{ role: RoleCode }>()
const styles = {
  admin: 'bg-info-soft text-accent',
  operator: 'bg-iconbox text-brand',
  observer: 'bg-mute-soft text-muted',
}
</script>
<template>
  <span
    class="inline-flex w-fit items-center rounded-[4px] px-[9px] py-1 text-[12px] font-medium whitespace-nowrap"
    :class="styles[role]"
    >{{ roleLabel(role) }}</span
  >
</template>
