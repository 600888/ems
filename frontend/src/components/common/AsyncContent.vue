<script setup lang="ts">
import ActionBtn from './ActionBtn.vue'
import EmptyState from './EmptyState.vue'
defineProps<{
  loading?: boolean
  error?: string
  empty?: boolean
  emptyTitle?: string
  emptyDescription?: string
}>()
const emit = defineEmits<{ retry: [] }>()
</script>
<template>
  <div class="flex min-h-0 flex-1 flex-col gap-5" :aria-busy="loading">
    <div v-if="loading" class="panel flex min-h-[280px] flex-1 flex-col p-8">
      <el-skeleton :rows="7" animated />
    </div>
    <EmptyState
      v-else-if="error"
      icon="cloud-off"
      title="暂时无法读取数据"
      :desc="error"
      tone="danger"
      :min-height="320"
      ><ActionBtn icon="refresh-cw" @click="emit('retry')">重新加载</ActionBtn></EmptyState
    >
    <EmptyState
      v-else-if="empty"
      icon="search-x"
      :title="emptyTitle ?? '没有匹配的记录'"
      :desc="emptyDescription"
      :min-height="280"
      ><slot name="empty-action"
    /></EmptyState>
    <slot v-else />
  </div>
</template>
