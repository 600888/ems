<script setup lang="ts">
import AppIcon from '@/components/common/AppIcon.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import { useMonitorStore } from '@/stores/monitor'

const monitor = useMonitorStore()
</script>

<template>
  <!-- 画板 11 · 设备身份与通信状态：不固定行高，标题行贴顶（与历史数据页页头一致） -->
  <div class="flex shrink-0 items-center justify-between">
    <div class="flex min-w-0 items-center gap-[10px]">
      <AppIcon :name="monitor.profile.icon" :size="24" class="shrink-0 text-muted" />
      <h2 class="truncate text-title leading-none font-semibold text-ink">
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

    <div class="flex shrink-0 flex-col items-end gap-[6px]">
      <span class="truncate text-caption leading-none text-subtle">
        {{ monitor.profile.code }} · 数据来源：{{ monitor.profile.dataSource }}
      </span>
      <div class="flex items-center gap-[7px]">
        <i class="dot" :class="monitor.paused ? 'text-warn' : 'text-ok'" />
        <span class="text-caption leading-none text-muted">
          更新于 {{ monitor.lastUpdated }} · {{ monitor.paused ? '已暂停刷新' : '每 2 秒刷新' }}
        </span>
        <button
          type="button"
          class="text-muted transition-colors hover:text-ink"
          aria-label="刷新"
          @click="monitor.refresh"
        >
          <AppIcon name="refresh-cw" :size="13" />
        </button>
      </div>
    </div>
  </div>
</template>
