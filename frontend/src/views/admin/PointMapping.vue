<script setup lang="ts">
import {ElMessage} from 'element-plus'
import {useRouter} from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import ConfigSteps from '@/components/admin/ConfigSteps.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import {useConfigStore, type MappingTab} from '@/stores/config'
import type {TabItem} from '@/types/ui'

const router = useRouter()
const config = useConfigStore()

const tabs: TabItem[] = [
  {key: 'all', label: '全部', count: config.mappingCounts.all},
  {key: 'yc', label: '遥测', count: config.mappingCounts.yc},
  {key: 'yx', label: '状态', count: config.mappingCounts.yx},
  {key: 'event', label: '事件', count: config.mappingCounts.event},
  {key: 'yk', label: '命令', count: config.mappingCounts.yk},
]

const dependencies = [
  {name: '运行监视', available: true},
  {name: '有功调度', available: true},
  {name: '无功调节', available: false},
]
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <ConfigSteps :current="3" />
    <InfoBar
      text="解析检查已全部通过 ｜ 已启用 26 项 · 已禁用 2 项 · 未配置 0 项 ｜ 绑定来源：PCS-125K_点表_v2.3.xlsx"
    />

    <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
      <div class="flex shrink-0 items-center justify-between px-4 py-3">
        <h3 class="text-[16px] font-semibold text-ink">固有点位映射</h3>
        <div class="flex items-center gap-[10px]">
          <el-select class="ctl-sm w-[128px]!" model-value="显示全部状态">
            <el-option label="显示全部状态" value="显示全部状态" />
            <el-option label="仅看启用" value="仅看启用" />
            <el-option label="仅看禁用" value="仅看禁用" />
          </el-select>
          <button
            type="button"
            class="flex h-[34px] items-center gap-[6px] rounded-[5px] border border-line bg-raised px-3 text-[12px] font-medium text-ink"
            @click="ElMessage.info('已套用型号模板 PCS-125K / v2.3')"
          >
            <AppIcon name="layers" :size="14" />
            套用型号模板
          </button>
        </div>
      </div>

      <UnderlineTabs
        :model-value="config.mappingTab"
        :items="tabs"
        :height="44"
        @update:model-value="config.setMappingTab($event as MappingTab)"
      />

      <div class="dt-head shrink-0">
        <div class="min-w-0 flex-1"><span class="dt-th">固有点位 / code</span></div>
        <div class="w-[110px] shrink-0"><span class="dt-th">类型 / 单位</span></div>
        <div class="w-[100px] shrink-0"><span class="dt-th">配置状态</span></div>
        <div class="w-[160px] shrink-0"><span class="dt-th">厂家点位来源</span></div>
        <div class="w-[160px] shrink-0"><span class="dt-th">转换 / 写入</span></div>
        <div class="w-[150px] shrink-0"><span class="dt-th">反馈规则</span></div>
        <div class="w-[80px] shrink-0"><span class="dt-th">校验</span></div>
      </div>

      <div class="min-h-0 flex-1 overflow-y-auto">
        <div v-for="row in config.filteredMapping" :key="row.code" class="dt-row h-[52px]">
          <div class="dt-cell flex min-w-0 flex-1 flex-col gap-[3px]">
            <span class="truncate text-[11px] font-semibold text-ink">{{ row.name }}</span>
            <span class="truncate font-mono text-[9px] text-subtle">{{ row.code }}</span>
          </div>
          <div class="dt-cell w-[110px] shrink-0 text-[11px] text-ink">{{ row.kind }} / {{ row.unit }}</div>
          <div class="dt-cell w-[100px] shrink-0">
            <StatusTag
              :tone="row.state === 'enabled' ? 'ok' : 'mute'"
              :label="row.state === 'enabled' ? '启用' : '禁用'"
              :dot="false"
            />
          </div>
          <div class="dt-cell w-[160px] shrink-0 truncate text-[11px] text-ink">{{ row.source }}</div>
          <div class="dt-cell w-[160px] shrink-0 truncate text-[10px] text-muted">{{ row.transform }}</div>
          <div class="dt-cell w-[150px] shrink-0 truncate text-[10px] text-muted">{{ row.feedback }}</div>
          <div class="dt-cell w-[80px] shrink-0">
            <span class="text-[10px] text-ok">✓ 通过</span>
          </div>
        </div>

        <p v-if="!config.filteredMapping.length" class="p-6 text-center text-[11px] text-muted">
          当前分类下暂无点位
        </p>
      </div>

      <div class="flex shrink-0 items-center gap-2 border-t border-line px-4 py-3">
        <AppIcon name="info" :size="14" class="text-accent" />
        <p class="text-[10px] text-muted">启停字共用寄存器，已声明互斥执行资源；禁用点位保留定义与原因。</p>
      </div>
    </section>

    <!-- 功能依赖检查 -->
    <section class="panel flex shrink-0 items-center gap-4 px-4 py-3">
      <h3 class="text-[12px] font-semibold text-ink">功能校验</h3>
      <div class="flex flex-1 items-center gap-[10px]">
        <StatusTag
          v-for="item in dependencies"
          :key="item.name"
          :tone="item.available ? 'ok' : 'mute'"
          :label="`${item.name}：${item.available ? '可用' : '不可用'}`"
          :dot="false"
        />
      </div>
    </section>

    <!-- 底部操作 -->
    <div class="flex shrink-0 items-center gap-3 rounded-[6px] border border-line bg-panel px-4 py-[10px]">
      <span class="text-[10px] text-muted">标准读取与控制命令分别绑定；当前仅为配置草稿</span>
      <div class="flex-1" />
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink"
        @click="router.push('/admin/devices/pcs01/points/import')"
      >
        上一步
      </button>
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink"
        @click="ElMessage.success('草稿已保存')"
      >
        保存草稿
      </button>
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-brand bg-brand px-[14px] text-[13px] font-medium text-white transition-all hover:brightness-110"
        @click="router.push('/admin/devices/pcs01/release')"
      >
        校验并预览差异
      </button>
    </div>
  </div>
</template>
