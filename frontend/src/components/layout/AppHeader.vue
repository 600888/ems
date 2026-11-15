<script setup lang="ts">
import { computed } from 'vue'
import { useMonitorStore } from '@/stores/monitor'
import { useAuthStore } from '@/stores/auth'
import { useAlarmStore } from '@/stores/alarm'
import { ElMessage } from 'element-plus'
import { useRoute, useRouter } from 'vue-router'
import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import ThemeToggle from '@/components/common/ThemeToggle.vue'
import AccountMenu from '@/components/identity/AccountMenu.vue'
import { monitorNav } from '@/domain/navigation'
import { useClock } from '@/composables/useClock'
defineProps<{ admin?: boolean }>()
const route = useRoute(),
  router = useRouter()
const monitor = useMonitorStore()
const auth = useAuthStore()
const alarm = useAlarmStore()
const inAlarms = computed(() => route.path.startsWith('/alarms'))
const serviceLabel = computed(() =>
  inAlarms.value
    ? alarm.error
      ? '告警服务不可用'
      : alarm.receivedAt
        ? '告警已更新'
        : '告警待加载'
    : monitor.error
      ? '数据服务不可用'
      : monitor.lastUpdated === '—'
        ? '数据待加载'
        : '数据已更新',
)
const { text: nowText } = useClock()
function go(to?: string) {
  if (to) void router.push(to)
  else ElMessage.info('该业务模块尚未接入')
}
</script>
<template>
  <header
    class="app-header flex shrink-0 items-center gap-3 border-b border-line bg-nav px-3 sm:gap-8 sm:px-6"
  >
    <div class="flex shrink-0 items-center gap-[10px]">
      <AppIcon name="zap" :size="26" class="text-accent" /><span class="text-title font-bold"
        >EMS</span
      ><span class="hidden text-lead text-muted sm:block">{{
        admin ? '后台管理' : '能源管理系统'
      }}</span>
    </div>
    <nav v-if="!admin" class="hidden h-full items-center gap-6 xl:flex" aria-label="前台导航">
      <button
        v-for="item in monitorNav"
        :key="item.key"
        :disabled="!item.to"
        :title="!item.to ? '尚未接通' : undefined"
        type="button"
        class="flex h-full items-center gap-2 border-b-2 text-body"
        :class="
          (item.key === 'monitor' && route.path.startsWith('/monitor')) ||
          (item.key === 'alarm' && route.path.startsWith('/alarms')) ||
          (item.key === 'history' && route.path.startsWith('/history'))
            ? 'border-accent font-semibold text-accent'
            : 'border-transparent text-muted hover:text-ink'
        "
        @click="go(item.to)"
      >
        <AppIcon :name="item.icon" :size="16" />{{ item.label }}
      </button>
    </nav>
    <el-dropdown v-if="!admin" class="xl:hidden!" trigger="click" @command="go">
      <button
        type="button"
        class="flex items-center gap-1 text-small text-muted"
        aria-label="运行模块导航"
      >
        <AppIcon name="menu" :size="18" /><span class="hidden sm:inline">运行模块</span>
      </button>
      <template #dropdown
        ><el-dropdown-menu
          ><el-dropdown-item
            v-for="item in monitorNav.filter((item) => item.to)"
            :key="item.key"
            :command="item.to"
            >{{ item.label }}</el-dropdown-item
          ></el-dropdown-menu
        ></template
      >
    </el-dropdown>
    <div class="flex-1" />
    <div class="flex min-w-0 shrink-0 items-center gap-2 sm:gap-4">
      <span
        v-if="!admin"
        class="hidden rounded-[5px] border border-line bg-panel px-3 py-2 text-small text-muted 2xl:inline-flex"
        >{{ (inAlarms ? alarm.allowedTree.tree[0]?.name : monitor.tree[0]?.name) ?? '站端' }}</span
      >
      <StatusTag
        class="hidden! lg:inline-flex!"
        :tone="admin ? 'accent' : (inAlarms ? alarm.error : monitor.error) ? 'warn' : 'mute'"
        :label="admin ? '后台配置' : serviceLabel"
      />
      <span class="hidden text-small text-muted 2xl:block">{{ nowText }}</span>
      <button
        type="button"
        aria-label="告警通知"
        class="hidden text-muted hover:text-ink md:block"
        :disabled="!auth.can('alarm.read')"
        @click="go('/alarms')"
      >
        <AppIcon name="bell" :size="17" />
      </button>
      <ThemeToggle /><AccountMenu :admin="admin" />
    </div>
  </header>
</template>
