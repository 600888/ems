<script setup lang="ts">
import {computed} from 'vue'

import AppIcon from '@/components/common/AppIcon.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import {useMonitorStore} from '@/stores/monitor'

const monitor = useMonitorStore()

const checks = ['设备在线，数据有效', '设备处于远方控制', '遥控权限已通过']

const current = computed(() => monitor.activeCommand)

const canExecute = computed(() => Boolean(current.value) && !monitor.controlRunning)

const commandState = computed(() => {
  if (monitor.controlRunning) return {tone: 'text-accent', icon: 'loader-circle', title: '正在执行', desc: '指令已发送，正在等待设备反馈…', spin: true}
  if (monitor.controlResult === 'success')
    return {tone: 'text-ok', icon: 'circle-check', title: '执行成功', desc: '设备反馈：控制命令已执行', spin: false}
  if (monitor.controlResult === 'failed')
    return {tone: 'text-danger', icon: 'circle-x', title: '执行失败', desc: '设备拒绝：控制权已变更。请重新核对。', spin: false}
  if (monitor.controlResult === 'timeout')
    return {tone: 'text-warn', icon: 'clock-3', title: '执行超时', desc: '结果未确认，请核对设备状态与操作记录。', spin: false}
  return null
})
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <!-- 遥控操作状态 -->
    <div class="panel flex h-[46px] shrink-0 items-center gap-[10px] px-4">
      <AppIcon name="shield-check" :size="16" class="text-ok" />
      <span class="text-[12px] text-ink">远方控制 · 通信正常 · 当前角色具备遥控权限</span>
      <div class="flex-1" />
      <span class="text-[11px] text-muted">操作人：值班员</span>
    </div>

    <div class="flex min-h-0 flex-1 gap-4">
      <section class="flex min-w-0 flex-1 flex-col gap-4">
        <!-- 遥控命令 -->
        <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
          <div class="panel-bar">
            <h3 class="text-[14px] font-semibold text-ink">遥控命令</h3>
            <span class="text-[11px] text-muted">{{ monitor.ykList.length }} 个控制点</span>
          </div>

          <div v-if="monitor.ykList.length" class="grid min-h-0 flex-1 grid-cols-2 content-start gap-3 overflow-y-auto p-4">
            <article
              v-for="command in monitor.ykList"
              :key="command.name"
              class="flex flex-col gap-2 rounded-[6px] border p-3 transition-colors"
              :class="
                command.disabledReason
                  ? 'border-line bg-surface opacity-70'
                  : current?.name === command.name
                    ? 'border-accent/60 bg-sel'
                    : 'border-line bg-surface hover:border-subtle'
              "
            >
              <div class="flex items-center justify-between gap-2">
                <span class="truncate text-[13px] font-semibold text-ink">{{ command.name }}</span>
                <button
                  type="button"
                  class="flex h-[28px] shrink-0 items-center gap-1 rounded-[4px] border px-2 text-[11px] font-medium"
                  :class="
                    command.disabledReason
                      ? 'cursor-not-allowed border-line bg-raised text-subtle'
                      : 'border-brand bg-brand text-white'
                  "
                  :disabled="Boolean(command.disabledReason)"
                  @click="monitor.openCommand(command)"
                >
                  {{ command.action }}
                </button>
              </div>
              <span class="text-[10px] text-muted">{{ command.desc }}</span>
              <span class="text-[10px] text-subtle">{{ command.current }}</span>
              <span v-if="command.disabledReason" class="text-[9px] text-warn">{{ command.disabledReason }}</span>
            </article>
          </div>

          <EmptyState
            v-else
            icon="lock-keyhole"
            title="无控制权限"
            desc="当前账号仅可查看实时数据；遥控入口已禁用并说明原因。"
            :min-height="200"
            class="m-4"
          />
        </section>

        <!-- 最近控制记录 -->
        <section class="panel flex h-[196px] shrink-0 flex-col overflow-hidden">
          <div class="panel-bar">
            <h3 class="text-[14px] font-semibold text-ink">最近控制记录</h3>
            <span class="text-[11px] text-accent">全部记录 →</span>
          </div>

          <div class="dt-head shrink-0">
            <div class="w-[100px] shrink-0"><span class="dt-th">操作时间</span></div>
            <div class="min-w-0 flex-1"><span class="dt-th">命令</span></div>
            <div class="w-[120px] shrink-0"><span class="dt-th">目标值</span></div>
            <div class="w-[110px] shrink-0"><span class="dt-th">操作人</span></div>
            <div class="w-[100px] shrink-0"><span class="dt-th">结果</span></div>
          </div>

          <div class="min-h-0 flex-1 overflow-y-auto">
            <div v-for="log in monitor.controlLogRows" :key="log.time" class="dt-row h-[44px]">
              <span class="dt-cell w-[100px] shrink-0 text-[11px] text-muted">{{ log.time }}</span>
              <span class="dt-cell min-w-0 flex-1 truncate text-[11px] text-ink">{{ log.command }}</span>
              <span class="dt-cell w-[120px] shrink-0 text-[11px] text-muted">{{ log.target }}</span>
              <span class="dt-cell w-[110px] shrink-0 text-[11px] text-muted">{{ log.operator }}</span>
              <span class="dt-cell w-[100px] shrink-0 text-[11px] text-ok">执行成功</span>
            </div>
          </div>
        </section>
      </section>

      <!-- 遥控确认 -->
      <aside class="panel flex w-[360px] shrink-0 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">遥控确认 · {{ current?.name ?? '未选择命令' }}</h3>
        </div>

        <div class="flex min-h-0 flex-1 flex-col gap-4 overflow-y-auto px-4 py-3">
          <div class="flex items-center gap-2 text-[10px] text-muted">
            <span class="text-accent">01 选择命令</span>
            <span>/</span>
            <span class="text-accent">02 确认执行</span>
            <span>/</span>
            <span>03 查看结果</span>
          </div>

          <div class="flex flex-col gap-2 rounded-[5px] bg-warnbox px-3 py-2">
            <span class="text-[13px] font-medium text-warn">停机后，PCS 将停止功率输出</span>
            <span class="text-[10px] text-muted">请确认当前调度允许，并核对目标设备。</span>
          </div>

          <div class="flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">目标设备</span>
              <span class="text-[12px] text-ink">储能单元 01 / PCS 01</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">控制命令</span>
              <span class="text-[12px] text-ink">{{ current?.name ?? '—' }}</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">当前状态</span>
              <span class="text-[12px] text-ink">并网放电 · 240.0 kW</span>
            </div>
            <div class="flex items-center justify-between">
              <span class="text-[11px] text-muted">目标状态</span>
              <span class="text-[12px] text-ink">停机 / 待机</span>
            </div>
          </div>

          <div class="flex flex-col gap-2">
            <span v-for="item in checks" :key="item" class="flex items-center gap-2 text-[11px] text-ok">
              <i class="dot" />{{ item }}
            </span>
          </div>

          <div class="flex flex-col gap-[7px]">
            <span class="text-[11px] text-muted">操作原因</span>
            <el-input v-model="monitor.controlReason" class="ctl" placeholder="填写操作原因" />
          </div>

          <el-checkbox :model-value="true">已核对设备和操作影响</el-checkbox>

          <p class="text-[10px] text-subtle">超时或失败时显示原因，可在操作记录中查看。</p>

          <div
            v-if="commandState"
            class="flex items-start gap-2 rounded-[5px] bg-surface px-3 py-2"
            :class="commandState.tone"
          >
            <AppIcon
              :name="commandState.icon"
              :size="14"
              :class="[commandState.tone, commandState.spin && 'animate-spin']"
            />
            <div class="flex flex-col gap-1">
              <span class="text-[11px] font-medium">{{ commandState.title }}</span>
              <span class="text-[10px] text-muted">{{ commandState.desc }}</span>
            </div>
          </div>

          <div class="flex-1" />

          <div class="flex items-center justify-end gap-[10px] pb-1">
            <button
              type="button"
              class="h-[36px] rounded-[5px] border border-line bg-raised px-[14px] text-[12px] font-medium text-ink"
              @click="monitor.closeCommand()"
            >
              取消
            </button>
            <button
              type="button"
              class="h-[36px] rounded-[5px] border border-brand bg-brand px-[14px] text-[12px] font-medium text-white transition-all hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-60"
              :disabled="!canExecute"
              @click="monitor.executeCommand()"
            >
              {{ monitor.controlRunning ? '执行中…' : `确认${current?.action ?? '执行'}` }}
            </button>
          </div>
        </div>
      </aside>
    </div>
  </div>
</template>
