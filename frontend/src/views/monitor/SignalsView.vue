<script setup lang="ts">
import { computed, ref } from 'vue'

import AppIcon from '@/components/common/AppIcon.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import { yxChanges } from '@/mocks/points'
import { useMonitorStore } from '@/stores/monitor'

const monitor = useMonitorStore()
const onlyAbnormal = ref(false)

const rows = computed(() =>
  onlyAbnormal.value ? monitor.yxList.filter((row) => row.level !== 'normal') : monitor.yxList,
)

const levelClass = {
  normal: 'text-ok',
  warn: 'text-warn',
  danger: 'text-danger',
  unknown: 'text-muted',
} as const

const alarmFields = [
  { label: '告警等级', value: '一级预警' },
  { label: '发生位置', value: '电池簇 02 / 电池包 01' },
  { label: '当前值 / 阈值', value: '5.4 °C / 5.0 °C' },
  { label: '持续时间', value: '3 分 32 秒' },
]
</script>

<template>
  <div class="flex min-h-0 flex-1 gap-4">
    <section class="panel flex min-w-0 flex-1 flex-col overflow-hidden">
      <!-- 遥信筛选 -->
      <div class="flex h-[52px] shrink-0 items-center gap-[10px] px-4">
        <button
          type="button"
          class="rounded-[4px] px-[9px] py-1 text-[11px] transition-colors"
          :class="!onlyAbnormal ? 'bg-sel text-accent' : 'bg-surface text-muted hover:text-ink'"
          @click="onlyAbnormal = false"
        >
          全部信号 {{ monitor.profile.tabCounts.signal }}
        </button>
        <button
          type="button"
          class="rounded-[4px] px-[9px] py-1 text-[11px] transition-colors"
          :class="onlyAbnormal ? 'bg-sel text-accent' : 'bg-surface text-muted hover:text-ink'"
          @click="onlyAbnormal = true"
        >
          仅看异常 1
        </button>

        <el-select class="ctl-sm w-[124px]!" model-value="全部分组">
          <el-option label="全部分组" value="全部分组" />
          <el-option label="运行状态" value="运行状态" />
          <el-option label="温度保护" value="温度保护" />
          <el-option label="电压保护" value="电压保护" />
        </el-select>

        <div class="flex-1" />

        <div class="flex items-center gap-[10px] text-[10px]">
          <span class="flex items-center gap-[5px] text-muted"
            ><i class="dot text-ok" />正常 / 未触发</span
          >
          <span class="flex items-center gap-[5px] text-muted"
            ><i class="dot text-warn" />预警</span
          >
          <span class="flex items-center gap-[5px] text-muted"
            ><i class="dot text-danger" />故障</span
          >
          <span class="flex items-center gap-[5px] text-muted"
            ><i class="dot text-subtle" />未知</span
          >
        </div>
      </div>

      <div class="dt-head shrink-0">
        <div class="min-w-0 flex-1"><span class="dt-th">信号名称</span></div>
        <div class="w-[120px] shrink-0"><span class="dt-th">当前状态</span></div>
        <div class="w-[90px] shrink-0"><span class="dt-th">原始值</span></div>
        <div class="w-[140px] shrink-0"><span class="dt-th">最近变位</span></div>
        <div class="w-[140px] shrink-0"><span class="dt-th">所属分组</span></div>
      </div>

      <div class="min-h-0 flex-1 overflow-y-auto">
        <template v-if="rows.length">
          <div v-for="row in rows" :key="row.name" class="dt-row h-[45px]">
            <span class="dt-cell min-w-0 flex-1 truncate text-[12px] text-ink">{{ row.name }}</span>
            <span class="dt-cell w-[120px] shrink-0 text-[12px]" :class="levelClass[row.level]">
              {{ row.state }}
            </span>
            <span class="dt-cell w-[90px] shrink-0 font-mono text-[11px] text-muted">{{
              row.raw
            }}</span>
            <span class="dt-cell w-[140px] shrink-0 text-[11px] text-muted">{{
              row.lastChange
            }}</span>
            <span class="dt-cell w-[140px] shrink-0 text-[11px] text-muted">{{ row.group }}</span>
          </div>
        </template>

        <EmptyState
          v-else
          icon="inbox"
          title="暂无遥信"
          desc="该设备暂无遥信信号。"
          :min-height="220"
          class="m-4"
        />
      </div>
    </section>

    <!-- 变位与告警 -->
    <aside class="flex w-[360px] shrink-0 flex-col gap-4">
      <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">最近变位</h3>
          <span class="text-[11px] text-accent">查看全部 →</span>
        </div>
        <div class="min-h-0 flex-1 overflow-y-auto">
          <div
            v-for="item in yxChanges"
            :key="item.time"
            class="flex flex-col gap-[5px] border-b border-line px-4 py-3 last:border-b-0"
          >
            <div class="flex items-center justify-between">
              <span class="text-[11px] font-medium text-ink">{{ item.name }}</span>
              <span class="text-[10px] text-muted">{{ item.time }}</span>
            </div>
            <span class="text-[10px] text-muted"
              >{{ item.from }} → {{ item.to }} · {{ item.scope }}</span
            >
          </div>
        </div>
      </section>

      <section class="panel flex shrink-0 flex-col gap-3 p-4">
        <div class="flex items-center gap-[7px]">
          <AppIcon name="triangle-alert" :size="14" class="text-warn" />
          <h3 class="text-[13px] font-semibold text-warn">当前告警 · 单体温差预警</h3>
        </div>
        <div class="flex flex-col gap-2">
          <div
            v-for="field in alarmFields"
            :key="field.label"
            class="flex items-center justify-between"
          >
            <span class="text-[10px] text-muted">{{ field.label }}</span>
            <span class="text-[11px] text-ink">{{ field.value }}</span>
          </div>
        </div>
        <span class="link-action text-[10px]">前往告警中心</span>
      </section>
    </aside>
  </div>
</template>
