<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import UserMenu from '@/components/common/UserMenu.vue'
import { useAuthStore } from '@/stores/auth'
import { errorMessage } from '@/api/http'
import type { UserMenuItem } from '@/types/ui'

const props = defineProps<{ admin?: boolean }>()
const auth = useAuthStore()
const router = useRouter()
const items = computed<UserMenuItem[]>(() => [
  ...(props.admin
    ? [{ key: 'monitor', label: '返回前台', icon: 'monitor', tone: 'accent' as const }]
    : auth.can('device.write') || auth.can('user.read')
      ? [{ key: 'admin', label: '后台管理', icon: 'settings', tone: 'accent' as const }]
      : []),
  ...(auth.can('user.read')
    ? [{ key: 'users', label: '用户管理', icon: 'users', tone: 'accent' as const }]
    : []),
  { key: 'profile', label: '个人中心', icon: 'user-round', tone: 'accent' },
  { key: 'logout', label: '退出登录', icon: 'log-out', tone: 'danger' },
])
async function select(key: string) {
  if (key === 'logout') {
    try {
      await auth.logout()
      await router.replace('/login')
    } catch (error) {
      ElMessage.error(errorMessage(error))
    }
    return
  }
  await router.push(
    key === 'users'
      ? '/admin/users'
      : key === 'admin'
        ? auth.can('device.write')
          ? '/admin/devices'
          : '/admin/users'
        : key === 'profile'
          ? '/profile'
          : auth.landing,
  )
}
</script>
<template>
  <UserMenu
    :name="auth.displayName"
    :role="auth.session?.scope_mode === 'all' ? '全部站与设备权限' : '按授权范围访问设备'"
    :items="items"
    @select="select"
  />
</template>
