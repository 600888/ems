<script setup lang="ts">
import { computed } from 'vue'
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'
import AppHeader from '@/components/layout/AppHeader.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import NewDeviceDrawer from '@/components/admin/NewDeviceDrawer.vue'
import { adminNav } from '@/mocks/nav'
import { useDeviceStore } from '@/stores/device'
import { useAuthStore } from '@/stores/auth'
const route = useRoute(),
  router = useRouter(),
  device = useDeviceStore(),
  auth = useAuthStore()
const isUsers = computed(() => route.path.startsWith('/admin/users'))
const activeKey = computed(() => (isUsers.value ? 'users' : 'devices'))
const visibleNav = computed(() =>
  adminNav.filter((item) =>
    item.key === 'users' ? auth.can('user.read') : auth.can('device.write'),
  ),
)
function go(item: (typeof adminNav)[number]) {
  if (item.to) void router.push(item.to)
  else ElMessage.info(`「${item.label}」尚未接入`)
}
</script>

<template>
  <div class="flex h-screen w-full flex-col overflow-hidden bg-surface">
    <AppHeader admin />

    <div class="flex min-h-0 flex-1">
      <!-- 后台主导航 -->
      <aside
        class="hidden w-[200px] shrink-0 flex-col gap-2 border-r border-line bg-nav px-3 py-6 md:flex"
      >
        <p class="px-2 text-[10px] font-medium text-subtle">配置与运维</p>

        <button
          v-for="item in visibleNav"
          :key="item.key"
          type="button"
          class="flex h-[46px] w-full items-center gap-3 rounded-[5px] px-[14px] transition-colors"
          :class="activeKey === item.key ? 'bg-sel' : 'hover:bg-raised'"
          @click="go(item)"
        >
          <AppIcon
            :name="item.icon"
            :size="17"
            :class="activeKey === item.key ? 'text-accent' : 'text-muted'"
          />
          <span
            class="text-[14px]"
            :class="activeKey === item.key ? 'font-semibold text-ink' : 'text-muted'"
          >
            {{ item.label }}
          </span>
        </button>

        <div class="flex-1" />

        <div class="flex flex-col gap-2 rounded-[6px] border border-line bg-sidebox p-3">
          <StatusTag tone="ok" label="站端已连接" />
          <p class="text-[10px] text-muted">采集服务 · 4 / 4 在线</p>
        </div>
        <p class="text-[9px] text-subtle">EMS · 站端管理</p>
      </aside>

      <!-- 工作区 -->
      <main
        class="flex min-w-0 flex-1 flex-col gap-5 overflow-hidden p-4 lg:p-6"
        :class="isUsers && '2xl:p-8'"
      >
        <div v-if="!isUsers" class="flex h-16 shrink-0 items-center justify-between">
          <div class="flex flex-col gap-[6px]">
            <h1 class="text-[26px] leading-none font-semibold text-ink">设备管理</h1>
            <p class="text-[12px] text-muted">后台配置 / 设备管理 · 接入与点表</p>
          </div>
          <div class="flex items-center gap-[10px]">
            <ActionBtn icon="layers" @click="ElMessage.info('型号模板库尚未接入')"
              >型号模板</ActionBtn
            >
            <ActionBtn
              v-if="auth.can('device.write')"
              variant="primary"
              icon="plus"
              @click="device.createDrawerOpen = true"
              >新建设备</ActionBtn
            >
          </div>
        </div>

        <router-view :key="String(route.params.userId || route.name)" />
      </main>
    </div>

    <NewDeviceDrawer v-if="auth.can('device.write')" />
  </div>
</template>
