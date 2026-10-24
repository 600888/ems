<script setup lang="ts">
import {computed} from 'vue'
import {ElMessage} from 'element-plus'
import {useRouter} from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import {useMonitorStore} from '@/stores/monitor'

const router = useRouter()
const monitor = useMonitorStore()

const qualityText = {good: '有效', timeout: '超时', invalid: '无效'} as const
const qualityClass = {good: 'text-ok', timeout: 'text-warn', invalid: 'text-danger'} as const

const selectionLabel = computed(() => `已选 ${monitor.selectedYcCodes.length} / 6 个测点`)

function addToCurve() {
  if (!monitor.selectedYcCodes.length) {
    ElMessage.warning('请先勾选需要对比的测点')
    return
  }
  ElMessage.success(`已加入实时曲线：${monitor.selectedYcCodes.length} 个测点`)
  router.push('/monitor/curves')
}
</script>

<template>
  <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
    <!-- 筛选与曲线入口 -->
    <div class="flex h-[58px] shrink-0 items-center gap-[10px] px-4">
      <el-input class="ctl-sm search-input w-[240px]!" placeholder="搜索测点名称 / 编码">
        <template #prefix>
          <AppIcon name="search" :size="14" class="text-subtle" />
        </template>
      </el-input>

      <el-select class="ctl-sm w-[124px]!" model-value="全部分组">
        <el-option label="全部分组" value="全部分组" />
        <el-option label="电池系统" value="电池系统" />
        <el-option label="电芯电压" value="电芯电压" />
        <el-option label="温度" value="温度" />
        <el-option label="绝缘" value="绝缘" />
      </el-select>

      <el-select class="ctl-sm w-[124px]!" model-value="全部质量">
        <el-option label="全部质量" value="全部质量" />
        <el-option label="仅看有效" value="仅看有效" />
        <el-option label="仅看超时" value="仅看超时" />
      </el-select>

      <div class="flex-1" />
      <span class="shrink-0 text-[11px] whitespace-nowrap text-muted">{{ selectionLabel }}</span>
      <button
        type="button"
        class="flex h-[34px] shrink-0 items-center gap-[6px] rounded-[5px] border border-brand bg-brand px-3 text-[12px] font-medium whitespace-nowrap text-white transition-all hover:brightness-110"
        @click="addToCurve"
      >
        <AppIcon name="chart-no-axes-combined" :size="14" />
        加入实时曲线
      </button>
    </div>

    <div class="dt-head shrink-0 h-[34px]">
      <div class="w-8 shrink-0"><span class="dt-th">选择</span></div>
      <div class="min-w-0 flex-1"><span class="dt-th">测点名称</span></div>
      <div class="w-[180px] shrink-0"><span class="dt-th">测点编码</span></div>
      <div class="w-[150px] shrink-0"><span class="dt-th">实时值</span></div>
      <div class="w-[62px] shrink-0"><span class="dt-th">单位</span></div>
      <div class="w-[120px] shrink-0"><span class="dt-th">数据质量</span></div>
      <div class="w-[200px] shrink-0"><span class="dt-th">采样时间</span></div>
      <div class="w-[76px] shrink-0"><span class="dt-th">趋势</span></div>
    </div>

    <div class="min-h-0 flex-1 overflow-y-auto">
      <template v-if="monitor.ycList.length">
        <div
          v-for="row in monitor.ycList"
          :key="row.code"
          class="dt-row h-[45px] transition-colors"
          :class="monitor.selectedYcCodes.includes(row.code) && 'dt-row-selected'"
        >
          <div class="w-8 shrink-0">
            <el-checkbox
              :model-value="monitor.selectedYcCodes.includes(row.code)"
              @change="monitor.toggleYc(row.code)"
            />
          </div>
          <span class="dt-cell min-w-0 flex-1 truncate text-[12px] text-ink">{{ row.name }}</span>
          <span class="dt-cell w-[180px] shrink-0 truncate font-mono text-[11px] text-muted">{{ row.code }}</span>
          <span class="dt-cell w-[150px] shrink-0 text-[12px] text-accent">{{ row.value }}</span>
          <span class="dt-cell w-[62px] shrink-0 text-[12px] text-muted">{{ row.unit }}</span>
          <span class="dt-cell w-[120px] shrink-0 text-[12px]" :class="qualityClass[row.quality]">
            {{ qualityText[row.quality] }}
          </span>
          <span class="dt-cell w-[200px] shrink-0 text-[12px] text-muted">{{ row.ts }}</span>
          <div class="dt-cell w-[76px] shrink-0">
            <button
              type="button"
              class="text-muted transition-colors hover:text-accent"
              aria-label="查看趋势"
              @click="router.push('/monitor/curves')"
            >
              <AppIcon name="chart-no-axes-combined" :size="15" />
            </button>
          </div>
        </div>
      </template>

      <EmptyState
        v-else
        icon="inbox"
        title="暂无测点"
        desc="该设备暂无遥测测点，不支持的操作入口已禁用。"
        :min-height="220"
        class="m-4"
      />
    </div>

    <div class="flex h-[46px] shrink-0 items-center justify-between px-4">
      <span class="text-[11px] text-muted">
        共 {{ monitor.profile.tabCounts.telemetry }} 个测点 · 当前显示 1–{{ Math.min(10, monitor.ycList.length) }} 条
      </span>
      <div class="flex items-center gap-3">
        <span class="text-[11px] text-muted">10 条 / 页</span>
        <el-pagination
          class="ems-pager"
          small
          background
          layout="prev, pager, next"
          :total="Math.max(monitor.profile.tabCounts.telemetry, 10)"
          :page-size="10"
        />
      </div>
    </div>
  </section>
</template>
