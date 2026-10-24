<script setup lang="ts">
import {ElMessage} from 'element-plus'
import {useRoute, useRouter} from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import ThemeToggle from '@/components/common/ThemeToggle.vue'
import UserMenu from '@/components/common/UserMenu.vue'
import {useClock} from '@/composables/useClock'
import {monitorNav} from '@/mocks/nav'
import type {UserMenuItem} from '@/types/ui'

const route = useRoute()
const router = useRouter()
const {text: nowText} = useClock()

const userMenu: UserMenuItem[] = [
  {key: 'admin', label: '后台管理', icon: 'settings', tone: 'accent'},
  {key: 'logout', label: '退出登录', icon: 'log-out', tone: 'danger'},
]

function go(item: (typeof monitorNav)[number]) {
  if (!item.to) {
    ElMessage.info(`「${item.label}」不在本次示例范围内`)
    return
  }
  router.push(item.to)
}

function onUserCommand(key: string) {
  if (key === 'admin') {
    router.push('/admin/devices')
    return
  }
  ElMessage.info('退出登录不在本次示例范围内')
}
</script>

<template>
  <div class="flex h-screen w-full flex-col overflow-hidden bg-surface">
    <!-- 前台全局导航：站点与活动告警并入顶栏右侧工具区，页面标题栏取消 -->
    <header class="flex h-16 shrink-0 items-center gap-11 border-b border-line bg-nav px-6">
      <div class="flex shrink-0 items-center gap-[10px]">
        <AppIcon name="zap" :size="26" class="text-accent" />
        <span class="text-[23px] leading-none font-bold text-ink">EMS</span>
        <span class="text-[15px] text-muted">能量管理系统</span>
      </div>

      <nav class="flex h-full items-center gap-[30px]">
        <button
          v-for="item in monitorNav"
          :key="item.key"
          type="button"
          class="flex h-full items-center gap-[7px] border-b-2 transition-colors"
          :class="
            route.path.startsWith('/monitor') && item.key === 'monitor'
              ? 'border-accent'
              : 'border-transparent'
          "
          @click="go(item)"
        >
          <AppIcon
            :name="item.icon"
            :size="16"
            :class="route.path.startsWith('/monitor') && item.key === 'monitor' ? 'text-accent' : 'text-muted'"
          />
          <span
            class="text-[13px]"
            :class="
              route.path.startsWith('/monitor') && item.key === 'monitor'
                ? 'font-semibold text-accent'
                : 'text-muted'
            "
          >
            {{ item.label }}
          </span>
        </button>
      </nav>

      <div class="flex-1" />

      <div class="flex shrink-0 items-center gap-[14px]">
        <button
          type="button"
          class="flex h-8 items-center gap-[6px] rounded-[5px] border border-line bg-panel px-[10px] transition-colors hover:border-subtle"
          @click="ElMessage.info('站点切换不在本次示例范围内')"
        >
          <AppIcon name="map-pin" :size="14" class="text-muted" />
          <span class="text-[12px] font-medium text-ink">华东示范储能站</span>
          <AppIcon name="chevron-down" :size="12" class="text-muted" />
        </button>

        <button
          type="button"
          class="flex h-8 items-center gap-[6px] rounded-[5px] bg-warn-soft px-[10px] transition-colors hover:brightness-110"
          @click="ElMessage.info('告警中心不在本次示例范围内')"
        >
          <AppIcon name="triangle-alert" :size="14" class="text-warn" />
          <span class="text-[12px] font-medium text-warn">2 条活动告警</span>
          <AppIcon name="chevron-right" :size="12" class="text-warn" />
        </button>

        <span class="h-[18px] w-px shrink-0 bg-line" />

        <StatusTag tone="ok" label="采集正常" />
        <span class="text-[12px] text-muted">{{ nowText }}</span>
        <button type="button" class="text-muted transition-colors hover:text-ink" aria-label="告警通知">
          <AppIcon name="bell" :size="17" />
        </button>
        <ThemeToggle />
        <UserMenu
          name="值班员"
          role="运行值班员 · 华东示范储能站"
          :items="userMenu"
          @select="onUserCommand"
        />
      </div>
    </header>

    <main class="flex min-h-0 flex-1 flex-col overflow-hidden px-6 pt-5 pb-6">
      <router-view />
    </main>
  </div>
</template>
