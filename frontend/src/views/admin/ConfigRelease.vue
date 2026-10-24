<script setup lang="ts">
import { ElMessage } from 'element-plus'
import { useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import ConfigSteps from '@/components/admin/ConfigSteps.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { useConfigStore } from '@/stores/config'

const router = useRouter()
const config = useConfigStore()

const diffChips = [
  { label: '新增', value: 12, cls: 'bg-sel text-accent' },
  { label: '修改', value: 4, cls: 'bg-warn-soft text-warn' },
  { label: '禁用', value: 2, cls: 'bg-danger-soft text-danger' },
  { label: '删除', value: 0, cls: 'bg-mute-soft text-muted' },
]

const chipClass: Record<string, string> = {
  新增: 'bg-sel text-accent',
  修改: 'bg-warn-soft text-warn',
  禁用: 'bg-danger-soft text-danger',
  删除: 'bg-mute-soft text-muted',
}

function publish() {
  if (!config.releaseReason.trim()) {
    ElMessage.warning('请填写变更原因')
    return
  }
  config.publish(() => ElMessage.success('v13 已发布，采集与策略服务将按序应用'))
}
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-3 overflow-y-auto">
    <ConfigSteps :current="4" />

    <!-- 校验结论 -->
    <section class="panel flex shrink-0 items-center gap-4 px-4 py-3">
      <div class="flex h-10 w-10 items-center justify-center rounded-full bg-ok-soft">
        <AppIcon name="circle-check" :size="20" class="text-ok" />
      </div>
      <div class="flex min-w-0 flex-1 flex-col gap-[5px]">
        <h3 class="text-[14px] font-semibold text-ink">配置校验通过，可发布为 v13</h3>
        <p class="truncate text-[10px] text-muted">
          12 项检查通过 · 0 项阻断 · 1 项提示：无功调节因型号不支持而保持禁用
        </p>
      </div>
      <span class="shrink-0 rounded-[4px] bg-ok-soft px-2 py-1 text-[10px] font-medium text-ok">
        离线校验完成
      </span>
    </section>

    <!-- 配置差异 -->
    <section class="panel flex h-[276px] shrink-0 flex-col overflow-hidden">
      <div class="flex shrink-0 items-center justify-between px-4 py-3">
        <div class="flex items-baseline gap-3">
          <h3 class="text-[14px] font-semibold text-ink">
            差异预览 <span class="font-normal text-muted">v12 → v13</span>
          </h3>
        </div>
        <div class="flex items-center gap-2">
          <span
            v-for="chip in diffChips"
            :key="chip.label"
            class="rounded-[4px] px-2 py-[3px] text-[10px] font-medium"
            :class="chip.cls"
          >
            {{ chip.label }} {{ chip.value }}
          </span>
        </div>
      </div>

      <div class="dt-head shrink-0">
        <div class="w-[90px] shrink-0"><span class="dt-th">变更类型</span></div>
        <div class="min-w-0 flex-1"><span class="dt-th">配置项</span></div>
        <div class="w-[200px] shrink-0"><span class="dt-th">当前生效 v12</span></div>
        <div class="w-[220px] shrink-0"><span class="dt-th">待发布 v13</span></div>
        <div class="w-[160px] shrink-0"><span class="dt-th">影响</span></div>
      </div>

      <div class="min-h-0 flex-1 overflow-y-auto">
        <div v-for="row in config.diffs" :key="row.item" class="dt-row h-[42px]">
          <div class="dt-cell w-[90px] shrink-0">
            <span
              class="rounded-[4px] px-2 py-[3px] text-[10px] font-medium"
              :class="chipClass[row.type]"
            >
              {{ row.type }}
            </span>
          </div>
          <div class="dt-cell min-w-0 flex-1 truncate text-[11px] font-medium text-ink">
            {{ row.item }}
          </div>
          <div class="dt-cell w-[200px] shrink-0 truncate text-[10px] text-muted">
            {{ row.current }}
          </div>
          <div class="dt-cell w-[220px] shrink-0 truncate text-[10px] text-ink">{{ row.next }}</div>
          <div class="dt-cell w-[160px] shrink-0 truncate text-[10px] text-muted">
            {{ row.impact }}
          </div>
        </div>
      </div>
    </section>

    <!-- 发布与应用过程 -->
    <section class="panel flex shrink-0 flex-col gap-2 px-4 py-3">
      <div class="flex items-center justify-between">
        <h3 class="text-[14px] font-semibold text-ink">版本应用进度</h3>
        <span class="text-[11px] text-muted">当前运行仍使用 v12</span>
      </div>

      <div class="grid grid-cols-4 gap-3">
        <div
          v-for="(stage, index) in ['已保存', '已发布', '采集已应用', '策略已应用']"
          :key="stage"
          class="flex flex-col gap-1 rounded-[5px] border px-3 py-2"
          :class="
            config.releaseStage > index + 1
              ? 'border-ok/40 bg-ok-soft'
              : config.releaseStage === index + 1
                ? 'border-accent/50 bg-sel'
                : 'border-line bg-surface'
          "
        >
          <span class="text-[10px] text-muted">{{ stage }}</span>
          <span
            class="text-[12px] font-medium"
            :class="
              config.releaseStage > index + 1
                ? 'text-ok'
                : config.releaseStage === index + 1
                  ? 'text-accent'
                  : 'text-subtle'
            "
          >
            {{
              index === 0
                ? config.releaseStage > 1
                  ? '✓ v13 已保存'
                  : 'v13 草稿'
                : index === 1
                  ? config.releaseStage > 2
                    ? '✓ v13 已发布'
                    : '○ 待发布'
                  : config.releaseStage > index + 1
                    ? '✓ v13 已应用'
                    : '○ 待应用'
            }}
          </span>
        </div>
      </div>

      <InfoBar text="相关服务应用同一版本后，才开放依赖该配置的控制能力。" />
    </section>

    <!-- 发布原因 -->
    <section class="panel flex shrink-0 flex-col gap-[7px] px-4 py-3">
      <span class="text-[11px] text-muted">变更原因 *</span>
      <el-input v-model="config.releaseReason" class="ctl" placeholder="填写本次配置变更的原因" />
    </section>

    <!-- 底部操作 -->
    <div
      class="flex shrink-0 items-center gap-3 rounded-[6px] border border-line bg-panel px-4 py-2"
    >
      <span class="text-[10px] text-muted">发布将生成不可变版本；历史回滚通过新版本完成</span>
      <div class="flex-1" />
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink"
        @click="router.push('/admin/devices/pcs01/points/mapping')"
      >
        返回映射
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
        class="h-[38px] rounded-[5px] border border-brand bg-brand px-[14px] text-[13px] font-medium text-white transition-all hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-60"
        :disabled="config.publishing"
        @click="publish"
      >
        {{ config.publishing ? '发布中…' : '发布配置 v13' }}
      </button>
    </div>
  </div>
</template>
