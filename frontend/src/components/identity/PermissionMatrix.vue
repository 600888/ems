<script setup lang="ts">
import { permissionMatrix, roles } from '@/domain/identity'
import PermissionMark from './PermissionMark.vue'
</script>
<template>
  <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
    <div class="min-h-0 overflow-auto">
      <table class="w-full min-w-[680px] border-collapse text-left text-[14px]">
        <thead class="sticky top-0 z-10 bg-raised">
          <tr class="h-[72px]">
            <th class="w-[38%] px-6 text-[14px] font-medium text-muted" scope="col">功能权限</th>
            <th v-for="role in roles" :key="role.code" class="px-4 text-center" scope="col">
              <span class="block text-[16px] font-semibold">{{ role.label }}</span
              ><span class="mt-1 block text-[12px] font-normal text-muted">{{
                role.description
              }}</span>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="row in permissionMatrix"
            :key="row.label"
            class="h-[40px] border-b border-line odd:bg-panel even:bg-rowalt"
          >
            <th scope="row" class="px-6 py-2 font-normal">{{ row.label }}</th>
            <td v-for="role in roles" :key="role.code" class="text-center">
              <PermissionMark :allowed="row.roles.includes(role.code)" />
            </td>
          </tr>
          <tr class="h-[40px]">
            <th class="px-6 py-2 font-normal" scope="row">个人资料、改密与自己的会话</th>
            <td v-for="role in roles" :key="role.code" class="text-center">
              <PermissionMark :allowed="true" />
            </td>
          </tr>
        </tbody>
      </table>
    </div>
    <footer
      class="mt-auto flex flex-wrap items-center gap-3 border-t border-line px-6 py-4 text-[12px] text-muted"
    >
      <PermissionMark :allowed="true" />允许<span class="w-3" /><PermissionMark
        :allowed="false"
      />禁止<span class="ml-auto">内置角色只读 · 权限仍受设备范围与安全联锁限制</span>
    </footer>
  </section>
</template>
