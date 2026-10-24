<script setup lang="ts">
import { computed } from 'vue'

import InfoBar from '@/components/common/InfoBar.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import { useDeviceStore } from '@/stores/device'

const device = useDeviceStore()
const row = computed(() => device.currentRow)

const capabilities = [
  { name: '运行监视', available: true, desc: '功率、状态及故障反馈已配置' },
  { name: '有功调度', available: true, desc: '双向功率设定与反馈已验收' },
  { name: '无功调节', available: false, desc: '型号不支持，相关点位已显式禁用' },
]

/** 配置口径摘要：后台不展示采集读数 */
const configMetrics = [
  { label: '固有点位', value: '26', unit: '项' },
  { label: '映射覆盖率', value: '93.1', unit: '%' },
  { label: '待发布草稿', value: '1', unit: '项' },
]
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4 overflow-y-auto">
    <!-- 设备档案与能力 -->
    <div class="flex shrink-0 gap-4">
      <section class="panel flex min-w-0 flex-1 flex-col gap-5 p-5">
        <div class="flex items-center justify-between">
          <h3 class="text-[14px] font-semibold text-ink">设备档案</h3>
          <span class="text-[11px] text-muted">设备 ID · dev000001</span>
        </div>

        <div class="flex gap-[18px]">
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">设备名称</span>
            <span class="field-value">{{ row?.name }}</span>
          </div>
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">设备编码</span>
            <span class="field-value">{{ row?.code }}</span>
          </div>
        </div>

        <div class="flex gap-[18px]">
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">设备类型 / 版本</span>
            <span class="field-value">{{ row?.type }} / v1.2</span>
          </div>
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">厂家 / 型号</span>
            <span class="field-value">{{ row?.vendor }}</span>
          </div>
        </div>

        <div class="flex gap-[18px]">
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">所属单元</span>
            <span class="field-value">储能单元 01 · ess000001</span>
          </div>
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">父级节点</span>
            <span class="field-value">储能单元 01</span>
          </div>
        </div>

        <div class="flex gap-[18px]">
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">额定有功功率</span>
            <span class="field-value">{{ row?.type === 'BMS' ? '215 kWh' : '125 kW' }}</span>
          </div>
          <div class="flex min-w-0 flex-1 flex-col gap-[7px]">
            <span class="field-label">固件版本</span>
            <span class="field-value">FW 2.1.6</span>
          </div>
        </div>
      </section>

      <section class="panel flex w-[332px] shrink-0 flex-col gap-[14px] p-5">
        <h3 class="text-[14px] font-semibold text-ink">设备能力</h3>

        <div v-for="(item, index) in capabilities" :key="item.name" class="flex flex-col gap-[7px]">
          <div class="flex items-center justify-between">
            <span class="text-[12px] text-ink">{{ item.name }}</span>
            <StatusTag
              :tone="item.available ? 'ok' : 'mute'"
              :label="item.available ? '可用' : '不可用'"
            />
          </div>
          <p class="text-[10px] text-muted">{{ item.desc }}</p>
          <div v-if="index < capabilities.length - 1" class="h-px w-full bg-line" />
        </div>

        <InfoBar text="控制入口以配置一致性与操作权限校验为准。" />
      </section>
    </div>

    <!-- 配置摘要与生效状态 -->
    <div class="flex shrink-0 gap-4">
      <section class="panel flex min-w-0 flex-1 flex-col gap-4 p-5">
        <div class="flex items-center justify-between">
          <h3 class="text-[14px] font-semibold text-ink">配置摘要</h3>
          <span class="text-[11px] text-muted">点表与映射统计</span>
        </div>
        <div class="flex justify-between">
          <div v-for="item in configMetrics" :key="item.label" class="flex flex-col gap-[7px]">
            <span class="text-[10px] text-muted">{{ item.label }}</span>
            <div class="flex items-baseline gap-[5px]">
              <span class="text-[24px] leading-none font-medium text-accent">{{ item.value }}</span>
              <span class="text-[10px] text-muted">{{ item.unit }}</span>
            </div>
          </div>
        </div>
      </section>

      <section class="panel flex w-[332px] shrink-0 flex-col gap-3 p-5">
        <div class="flex items-center justify-between">
          <h3 class="text-[14px] font-semibold text-ink">配置一致性</h3>
          <span class="text-[11px] text-muted">v12</span>
        </div>
        <p class="text-[10px] text-ok">✓ 已发布 → ✓ 采集已应用 → ✓ 策略已应用</p>
        <p class="text-[10px] text-muted">生效时间 2026-10-22 13:40:12</p>
      </section>
    </div>

    <InfoBar text="层级节点可不配置独立通信连接；通信与点表绑定在实际采集设备上维护。" />
  </div>
</template>
