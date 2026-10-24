<script setup lang="ts">
import { ElMessage } from 'element-plus'

import AppIcon from '@/components/common/AppIcon.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import { useConfigStore } from '@/stores/config'

const config = useConfigStore()

const details = [
  { label: '连接目标（示例）', value: '192.0.2.10 : 502' },
  { label: '共享通信资源', value: 'res01 · PCS 采集网口' },
  { label: '轮询周期 / 超时', value: '1,000 ms / 3,000 ms' },
  { label: '采集 / 控制', value: 'telemetry_in / control_out' },
]

/** 通道配置参数，取代运行期的通信健康度 */
const channelParams = [
  { label: '功能码', value: '03', tone: 'text-accent' },
  { label: '响应超时', value: '1,000 ms', tone: 'text-accent' },
  { label: '重试次数', value: '3 次', tone: 'text-accent' },
]
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
      <div class="flex shrink-0 items-center justify-between p-[18px]">
        <div class="flex flex-col gap-[5px]">
          <h3 class="text-[16px] font-semibold text-ink">采集与控制通道</h3>
          <p class="text-[11px] text-muted">同一设备可绑定多个通道；操作角色与连接方式分别配置</p>
        </div>
        <button
          type="button"
          class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-brand bg-brand px-[14px] text-[13px] font-medium text-white transition-all hover:brightness-110"
          @click="ElMessage.info('新增绑定不在本次示例范围内')"
        >
          <AppIcon name="plus" :size="15" />
          新增绑定
        </button>
      </div>

      <div class="dt-head shrink-0">
        <div class="w-[224px] shrink-0"><span class="dt-th">绑定名称 / 通道</span></div>
        <div class="w-[146px] shrink-0"><span class="dt-th">操作角色</span></div>
        <div class="w-[168px] shrink-0"><span class="dt-th">协议 / Profile</span></div>
        <div class="w-[122px] shrink-0"><span class="dt-th">设备地址</span></div>
        <div class="w-[190px] shrink-0"><span class="dt-th">配置状态</span></div>
        <div class="w-[138px] shrink-0"><span class="dt-th">配置版本</span></div>
        <div class="min-w-0 flex-1"><span class="dt-th">操作</span></div>
      </div>

      <div class="min-h-0 flex-1 overflow-y-auto">
        <div v-for="row in config.bindingRows" :key="row.name" class="dt-row h-[52px]">
          <div class="dt-cell flex w-[224px] shrink-0 flex-col gap-[3px]">
            <span class="truncate text-[11px] font-semibold text-ink">{{ row.name }}</span>
            <span class="truncate text-[9px] text-subtle">{{ row.resource }}</span>
          </div>
          <div class="dt-cell w-[146px] shrink-0 truncate text-[11px] text-ink">{{ row.role }}</div>
          <div class="dt-cell flex w-[168px] shrink-0 flex-col gap-[3px]">
            <span class="text-[11px] text-ink">{{ row.protocol }}</span>
            <span class="text-[9px] text-subtle">{{ row.profile }}</span>
          </div>
          <div class="dt-cell w-[122px] shrink-0 text-[11px] text-ink">{{ row.address }}</div>
          <div class="dt-cell w-[190px] shrink-0">
            <StatusTag
              :tone="row.state === 'enabled' ? 'ok' : 'mute'"
              :label="row.state === 'enabled' ? '已启用' : '已停用'"
            />
          </div>
          <div class="dt-cell w-[138px] shrink-0 text-[11px] text-ink">{{ row.version }}</div>
          <div class="dt-cell flex min-w-0 flex-1 items-center">
            <span class="link-action" @click="ElMessage.info('通道编辑不在本次示例范围内')"
              >编辑</span
            >
          </div>
        </div>
      </div>

      <div class="shrink-0 p-4">
        <InfoBar text="上送映射在通信管理中独立维护；厂家采集地址与上送地址分别配置。" />
      </div>
    </section>

    <div class="flex shrink-0 gap-4">
      <section class="panel flex min-w-0 flex-1 flex-col gap-4 p-5">
        <div class="flex items-center justify-between">
          <h3 class="text-[14px] font-semibold text-ink">ch01 · 主采集连接</h3>
          <span class="text-[11px] text-muted">主用通道</span>
        </div>
        <div class="grid grid-cols-2 gap-x-[18px] gap-y-4">
          <div v-for="item in details" :key="item.label" class="flex flex-col gap-[7px]">
            <span class="field-label">{{ item.label }}</span>
            <span class="field-value">{{ item.value }}</span>
          </div>
        </div>
      </section>

      <section class="panel flex w-[332px] shrink-0 flex-col gap-3 p-5">
        <div class="flex items-center justify-between">
          <h3 class="text-[14px] font-semibold text-ink">通信参数</h3>
          <span class="text-[11px] text-muted">ch01 · 主采集</span>
        </div>
        <div
          v-for="item in channelParams"
          :key="item.label"
          class="flex items-center justify-between"
        >
          <span class="text-[11px] text-muted">{{ item.label }}</span>
          <span class="text-[13px] font-medium" :class="item.tone">{{ item.value }}</span>
        </div>
      </section>
    </div>
  </div>
</template>
