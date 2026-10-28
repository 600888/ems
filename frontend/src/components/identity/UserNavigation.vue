<script setup lang="ts">
import { computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
const route = useRoute(),
  router = useRouter()
const items = [
  { key: 'users', label: '用户账号' },
  { key: 'roles', label: '角色权限' },
  { key: 'login-policy', label: '登录保护' },
]
const active = computed(() => String(route.name))
</script>
<template>
  <UnderlineTabs
    :items="items"
    :model-value="active"
    :height="48"
    class="px-0! [&_button]:text-[15px]!"
    @update:model-value="router.push({ name: $event })"
  />
</template>
