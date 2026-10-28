<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElMessage } from 'element-plus'

import AppIcon from '@/components/common/AppIcon.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import { useMonitorStore } from '@/stores/monitor'

const monitor = useMonitorStore()
const reason = ref('按值班计划调整放电功率')

const changed = computed(() => monitor.ytList.filter((row) => row.modified !== row.current))

const allValid = computed(() =>
  monitor.ytList.every((row) => {
    const value = Number(row.modified)
    return Number.isFinite(value) && value >= row.min && value <= row.max
  }),
)

const activeRow = computed(() => changed.value[0] ?? monitor.ytList[0])

function submit() {
  if (!monitor.ytList.length || !changed.value.length) return
  if (!allValid.value) {
    ElMessage.warning('存在超出允许范围的设定值，请修正后下发')
    return
  }
  ElMessage.success(`${activeRow.value?.name ?? '设定值'} 已下发，正在等待设备反馈`)
}
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <div class="panel flex h-[46px] shrink-0 items-center gap-[10px] px-4">
      <AppIcon name="sliders-horizontal" :size="16" class="text-accent" />
      <span class="text-[12px] text-ink">当前控制权：EMS 站端 · 远方控制 · 设备在线</span>
      <div class="flex-1" />
      <span class="text-[11px] text-muted">设定值经确认后下发设备</span>
    </div>

    <div class="flex min-h-0 flex-1 gap-4">
      <section class="panel flex min-w-0 flex-1 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">遥调 · 运行设定值</h3>
          <span class="text-[11px] text-muted"
            >{{ monitor.profile.tabCounts.setpoint }} 个调节测点</span
          >
        </div>

        <template v-if="monitor.ytList.length">
          <div class="dt-head shrink-0">
            <div class="min-w-0 flex-1"><span class="dt-th">测点名称</span></div>
            <div class="w-[120px] shrink-0"><span class="dt-th">当前设定</span></div>
            <div class="w-[160px] shrink-0"><span class="dt-th">修改值</span></div>
            <div class="w-[90px] shrink-0"><span class="dt-th">单位</span></div>
            <div class="w-[160px] shrink-0"><span class="dt-th">允许范围</span></div>
          </div>

          <div class="min-h-0 flex-1 overflow-y-auto">
            <div v-for="row in monitor.ytList" :key="row.name" class="dt-row h-[56px]">
              <span class="dt-cell min-w-0 flex-1 truncate text-[12px] text-ink">{{
                row.name
              }}</span>
              <span class="dt-cell w-[120px] shrink-0 text-[12px] text-muted">{{
                row.current
              }}</span>
              <div class="dt-cell w-[160px] shrink-0">
                <el-input
                  class="ctl w-[110px]!"
                  :model-value="row.modified"
                  @update:model-value="monitor.setYtValue(row.name, String($event))"
                />
              </div>
              <span class="dt-cell w-[90px] shrink-0 text-[12px] text-muted">{{ row.unit }}</span>
              <span class="dt-cell w-[160px] shrink-0 text-[11px] text-muted">
                {{ row.min }} ~ {{ row.max }}
              </span>
            </div>
          </div>

          <div class="flex shrink-0 flex-col gap-1 border-t border-line px-4 py-3">
            <span class="text-[11px]" :class="allValid ? 'text-ok' : 'text-warn'">
              {{
                allValid
                  ? `${changed.length} 个设定值已修改，范围校验通过`
                  : '存在超出允许范围的设定值，请修正后下发'
              }}
            </span>
            <span class="text-[10px] text-subtle">有功功率方向：正值为放电，负值为充电。</span>
            <span class="text-[10px] text-subtle">无功功率与功率因数按设备当前控制模式开放。</span>
          </div>
        </template>

        <EmptyState
          v-else
          icon="inbox"
          title="暂无测点"
          desc="该设备暂无遥调测点，不支持的操作入口禁用，并显示原因。"
          :min-height="220"
          class="m-4"
        />
      </section>

      <!-- 遥调下发确认 -->
      <aside class="panel flex w-[360px] shrink-0 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">
            遥调确认 · {{ activeRow?.name ?? '未选择测点' }}
          </h3>
        </div>

        <div class="flex min-h-0 flex-1 flex-col gap-4 overflow-y-auto px-4 py-3">
          <span class="text-[11px] text-muted">储能单元 01 / PCS 01</span>

          <div class="flex flex-col gap-2 rounded-[5px] bg-surface px-3 py-3">
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">当前设定</span>
              <span class="text-[13px] text-muted">
                {{ activeRow?.current }} <span class="text-[10px]">{{ activeRow?.unit }}</span>
              </span>
            </div>
            <AppIcon name="arrow-right" :size="14" class="self-center text-subtle" />
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">修改为</span>
              <span class="text-[20px] leading-none font-semibold text-accent">
                {{ activeRow?.modified }}
                <span class="text-[11px] font-normal text-muted">{{ activeRow?.unit }}</span>
              </span>
            </div>
          </div>

          <div class="flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">允许范围</span>
              <span class="text-[11px] text-ink">
                {{ activeRow?.min }} ~ {{ activeRow?.max }} {{ activeRow?.unit }}
              </span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">设备额定功率</span>
              <span class="text-[11px] text-ink">500 kW</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">放电功率限值</span>
              <span class="text-[11px] text-ink">500 kW</span>
            </div>
          </div>

          <span class="flex items-center gap-2 text-[11px] text-ok"
            ><i class="dot" />当前控制权与操作权限有效</span
          >

          <div class="flex flex-col gap-[7px]">
            <span class="text-[11px] text-muted">调整原因</span>
            <el-input v-model="reason" class="ctl" placeholder="填写调整原因" />
          </div>

          <p class="text-[10px] text-subtle">执行后核对设备反馈值；超时或失败时显示原因。</p>

          <div class="flex-1" />

          <div class="flex items-center justify-end gap-[10px] pb-1">
            <button
              type="button"
              class="h-[36px] rounded-[5px] border border-line bg-raised px-[14px] text-[12px] font-medium text-ink"
              @click="activeRow && monitor.resetYtValue(activeRow.name)"
            >
              恢复当前值
            </button>
            <button
              type="button"
              class="h-[36px] rounded-[5px] border border-brand bg-brand px-[14px] text-[12px] font-medium text-white transition-all hover:brightness-110"
              @click="submit"
            >
              确认下发
            </button>
          </div>
        </div>
      </aside>
    </div>
  </div>
</template>
