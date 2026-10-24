<script setup lang="ts">
import { computed } from 'vue'
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import ThemeToggle from '@/components/common/ThemeToggle.vue'
import UserMenu from '@/components/common/UserMenu.vue'
import NewDeviceDrawer from '@/components/admin/NewDeviceDrawer.vue'
import { useClock } from '@/composables/useClock'
import { adminNav } from '@/mocks/nav'
import { useDeviceStore } from '@/stores/device'
import type { UserMenuItem } from '@/types/ui'

const route = useRoute()
const router = useRouter()
const device = useDeviceStore()
const { text: nowText } = useClock()

const userMenu: UserMenuItem[] = [
  { key: 'monitor', label: '返回前台', icon: 'monitor', tone: 'accent' },
  { key: 'logout', label: '退出登录', icon: 'log-out', tone: 'danger' },
]

function onUserCommand(key: string) {
  if (key === 'monitor') {
    router.push('/monitor/overview')
    return
  }
  ElMessage.info('退出登录不在本次示例范围内')
}

const activeKey = computed(() =>
  route.path.startsWith('/admin/devices') || route.path.startsWith('/admin/hierarchy')
    ? 'devices'
    : '',
)

function go(item: (typeof adminNav)[number]) {
  if (!item.to) {
    ElMessage.info(`「${item.label}」不在本次示例范围内`)
    return
  }
  router.push(item.to)
}
</script>

<template>
  <div class="flex h-screen w-full flex-col overflow-hidden bg-surface">
    <!-- 全局顶栏 -->
    <header
      class="flex h-16 shrink-0 items-center justify-between border-b border-line bg-nav px-6"
    >
      <div class="flex items-center gap-[10px]">
        <AppIcon name="zap" :size="26" class="text-accent" />
        <span class="text-[23px] leading-none font-bold text-ink">EMS</span>
        <span class="text-[15px] text-muted">后台管理</span>
      </div>

      <div class="flex items-center gap-[22px]">
        <StatusTag tone="ok" label="后台服务正常" />
        <span class="text-[12px] text-muted">{{ nowText }}</span>
        <button
          type="button"
          class="text-muted transition-colors hover:text-ink"
          aria-label="告警通知"
        >
          <AppIcon name="bell" :size="17" />
        </button>
        <ThemeToggle />
        <UserMenu
          name="管理员"
          role="系统管理员 · 全部站点权限"
          :items="userMenu"
          @select="onUserCommand"
        />
      </div>
    </header>

    <div class="flex min-h-0 flex-1">
      <!-- 后台主导航 -->
      <aside class="flex w-[200px] shrink-0 flex-col gap-2 border-r border-line bg-nav px-3 py-6">
        <p class="px-2 text-[10px] font-medium text-subtle">配置与运维</p>

        <button
          v-for="item in adminNav"
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
        <p class="text-[9px] text-subtle">EMS ADMIN / V1.0</p>
      </aside>

      <!-- 工作区 -->
      <main class="flex min-w-0 flex-1 flex-col gap-5 overflow-hidden p-6">
        <div class="flex h-16 shrink-0 items-center justify-between">
          <div class="flex flex-col gap-[6px]">
            <h1 class="text-[26px] leading-none font-semibold text-ink">设备管理</h1>
            <p class="text-[12px] text-muted">后台配置 / 设备管理 · 接入、点表与配置版本</p>
          </div>
          <div class="flex items-center gap-[10px]">
            <button
              type="button"
              class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink transition-colors hover:border-subtle"
              @click="ElMessage.info('型号模板库不在本次示例范围内')"
            >
              <AppIcon name="layers" :size="15" />
              型号模板
            </button>
            <button
              type="button"
              class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-brand bg-brand px-[14px] text-[13px] font-medium text-white transition-all hover:brightness-110"
              @click="device.createDrawerOpen = true"
            >
              <AppIcon name="plus" :size="15" />
              新建设备
            </button>
          </div>
        </div>

        <router-view />
      </main>
    </div>

    <NewDeviceDrawer />
  </div>
</template>
