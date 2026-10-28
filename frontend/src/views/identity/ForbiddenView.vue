<script setup lang="ts">
import { useRouter } from 'vue-router'
import EmptyState from '@/components/common/EmptyState.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import { useAuthStore } from '@/stores/auth'
const auth = useAuthStore(),
  router = useRouter()
</script>
<template>
  <div class="flex min-h-screen items-center justify-center bg-surface p-6">
    <EmptyState
      title="没有操作权限"
      desc="请联系管理员确认角色和设备范围。"
      icon="shield"
      tone="danger"
      :min-height="320"
      class="w-full max-w-[540px]"
      ><ActionBtn variant="primary" @click="router.replace(auth.landing)"
        >返回工作台</ActionBtn
      ></EmptyState
    >
  </div>
</template>
