<script setup lang="ts">
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'
import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import ThemeToggle from '@/components/common/ThemeToggle.vue'
import AccountMenu from '@/components/identity/AccountMenu.vue'
import { monitorNav } from '@/mocks/nav'
import { useClock } from '@/composables/useClock'
defineProps<{ admin?: boolean }>()
const route = useRoute(),
  router = useRouter()
const { text: nowText } = useClock()
function go(to?: string) {
  if (to) void router.push(to)
  else ElMessage.info('该业务模块尚未接入')
}
</script>
<template>
  <header
    class="flex h-16 shrink-0 items-center gap-3 border-b border-line bg-nav px-3 sm:gap-8 sm:px-6"
  >
    <div class="flex shrink-0 items-center gap-[10px]">
      <AppIcon name="zap" :size="26" class="text-accent" /><span class="text-[23px] font-bold"
        >EMS</span
      ><span class="hidden text-[15px] text-muted sm:block">{{
        admin ? '后台管理' : '能源管理系统'
      }}</span>
    </div>
    <nav v-if="!admin" class="hidden h-full items-center gap-6 xl:flex" aria-label="前台导航">
      <button
        v-for="item in monitorNav"
        :key="item.key"
        type="button"
        class="flex h-full items-center gap-2 border-b-2 text-[13px]"
        :class="
          route.path.startsWith('/monitor') && item.key === 'monitor'
            ? 'border-accent font-semibold text-accent'
            : 'border-transparent text-muted hover:text-ink'
        "
        @click="go(item.to)"
      >
        <AppIcon :name="item.icon" :size="16" />{{ item.label }}
      </button>
    </nav>
    <div class="flex-1" />
    <div class="flex min-w-0 shrink-0 items-center gap-2 sm:gap-4">
      <span
        v-if="!admin"
        class="hidden rounded-[5px] border border-line bg-panel px-3 py-2 text-[12px] text-muted 2xl:inline-flex"
        >华东示范储能站</span
      >
      <StatusTag
        class="hidden! lg:inline-flex!"
        tone="ok"
        :label="admin ? '后台服务正常' : '采集正常'"
      />
      <span class="hidden text-[12px] text-muted 2xl:block">{{ nowText }}</span>
      <button
        type="button"
        aria-label="告警通知"
        class="hidden text-muted hover:text-ink md:block"
        @click="ElMessage.info('当前账号没有新的系统通知')"
      >
        <AppIcon name="bell" :size="17" />
      </button>
      <ThemeToggle /><AccountMenu :admin="admin" />
    </div>
  </header>
</template>
