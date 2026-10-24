<script setup lang="ts">
import { ElMessage } from 'element-plus'

import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import { useMonitorStore } from '@/stores/monitor'

const monitor = useMonitorStore()
</script>

<template>
  <div class="flex h-[76px] shrink-0 items-center justify-between">
    <div class="flex min-w-0 flex-col gap-2">
      <span class="truncate text-[11px] text-muted">{{ monitor.profile.parentPath }}</span>
      <div class="flex items-center gap-[10px]">
        <AppIcon :name="monitor.profile.icon" :size="20" class="text-muted" />
        <h2 class="truncate text-[21px] leading-none font-semibold text-ink">
          {{ monitor.profile.name }}
        </h2>
        <StatusTag
          :tone="
            monitor.profile.status === 'alarm'
              ? 'warn'
              : monitor.profile.status === 'offline'
                ? 'mute'
                : 'ok'
          "
          :label="
            monitor.profile.status === 'alarm'
              ? '告警'
              : monitor.profile.status === 'offline'
                ? '离线'
                : '在线'
          "
        />
      </div>
    </div>

    <div class="flex shrink-0 flex-col items-end gap-2">
      <span class="text-[11px] text-muted">
        {{ monitor.profile.code }} · 数据来源：{{ monitor.profile.dataSource }}
      </span>
      <div class="flex items-center gap-[7px]">
        <i class="dot" :class="monitor.paused ? 'text-warn' : 'text-ok'" />
        <span class="text-[11px] text-muted">
          更新于 {{ monitor.lastUpdated }} · {{ monitor.paused ? '已暂停刷新' : '每 2 秒刷新' }}
        </span>
        <button
          type="button"
          class="text-muted transition-colors hover:text-ink"
          aria-label="刷新"
          @click="ElMessage.success('已刷新当前设备实时数据')"
        >
          <AppIcon name="refresh-cw" :size="14" />
        </button>
      </div>
    </div>
  </div>
</template>
