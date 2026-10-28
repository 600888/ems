<script setup lang="ts">
import PageHeading from '@/components/common/PageHeading.vue'
import UserNavigation from '@/components/identity/UserNavigation.vue'
import PermissionMatrix from '@/components/identity/PermissionMatrix.vue'
</script>
<template>
  <div class="identity-page">
    <PageHeading
      title="角色权限"
      description="三个内置角色，一套明确的操作边界；设备范围由用户单独授权。"
      breadcrumb="系统管理 / 用户与权限"
    /><UserNavigation /><PermissionMatrix />
  </div>
</template>
