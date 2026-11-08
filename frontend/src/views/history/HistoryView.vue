<script setup lang="ts">
import { computed, onMounted } from 'vue'

import HistoryPointTree from '@/components/history/HistoryPointTree.vue'
import LineChart from '@/components/charts/LineChart.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import {
  HISTORY_COLORS,
  HISTORY_INTERVALS,
  useHistoryStore,
  type QuickRangeKey,
} from '@/stores/history'

const history = useHistoryStore()

const quickRanges: { key: QuickRangeKey; label: string }[] = [
  { key: '1h', label: '近 1 小时' },
  { key: '24h', label: '近 24 小时' },
  { key: '7d', label: '近 7 天' },
  { key: '30d', label: '近 30 天' },
]

const kindFilters: { key: 'all' | 'yc' | 'yx'; label: string }[] = [
  { key: 'all', label: '全部' },
  { key: 'yc', label: '遥测' },
  { key: 'yx', label: '遥信' },
]

/** 自定义时间范围绑定：空数组视为未选择 */
const customBinding = computed<[Date, Date] | null>({
  get: (): [Date, Date] | null =>
    history.customRange
      ? [new Date(history.customRange[0]), new Date(history.customRange[1])]
      : null,
  set: (value: [Date, Date] | null) => {
    if (value && value.length === 2) {
      history.quickRange = '24h'
      history.customRange = [+value[0], +value[1]]
    } else {
      history.customRange = null
    }
  },
})

function pickQuick(key: QuickRangeKey) {
  history.quickRange = key
  history.customRange = null
}

function setKind(key: 'all' | 'yc' | 'yx') {
  history.kindFilter = key
}

function setInterval(value: string) {
  history.interval = value
}

/** 已选测点 chip 的系列色，与图例一致 */
function chipColor(index: number) {
  return HISTORY_COLORS[index % HISTORY_COLORS.length]!
}

function legendName(name: string, unit: string) {
  return unit ? `${name} (${unit})` : name
}

const unitSummary = computed(() => {
  const units = [...new Set(history.visiblePoints.map((p) => p.unit).filter(Boolean))]
  return units.length ? units.join(' / ') : '—'
})

const emptyText = computed(() => {
  if (!history.visiblePoints.length) return '在左侧设备树勾选设备或测点，即可查询历史数据。'
  if (history.busy) return '正在查询…'
  if (history.lastQueryAt === '—') return '点击「查询」加载所选测点的历史数据。'
  return '所选时间范围内没有数据，请调整范围或测点后重新查询。'
})

onMounted(() => {
  void history.load()
})
</script>

<template>
  <div class="flush-workspace">
    <HistoryPointTree />

    <section
      class="flex min-h-0 min-w-0 flex-1 flex-col gap-3 overflow-y-auto pt-3.5 pr-5 pb-4 pl-5"
    >
      <!-- 页头 -->
      <div class="flex items-center gap-3">
        <AppIcon name="chart-no-axes-combined" :size="20" class="text-accent" />
        <h1 class="text-lead font-semibold text-ink">历史数据</h1>
        <span class="hidden text-small text-muted lg:block"
          >查询、对比与导出遥测 / 遥信历史数据</span
        >
        <div class="flex-1" />
        <span class="text-caption text-subtle">数据截至 {{ history.lastQueryAt }}</span>
        <ActionBtn :disabled="!history.tableRows.length" @click="history.exportCsv"
          >导出 CSV</ActionBtn
        >
      </div>

      <!-- 查询条件栏 -->
      <div class="panel flex flex-wrap items-center gap-2.5 px-4 py-2.5">
        <span class="text-caption text-subtle">时间范围</span>
        <div class="flex items-center gap-1.5">
          <button
            v-for="range in quickRanges"
            :key="range.key"
            type="button"
            class="rounded-[5px] px-3 py-[6px] text-small transition-colors"
            :class="
              !history.customRange && history.quickRange === range.key
                ? 'bg-brand text-white'
                : 'bg-raised text-muted hover:text-ink'
            "
            @click="pickQuick(range.key)"
          >
            {{ range.label }}
          </button>
        </div>
        <el-date-picker
          v-model="customBinding"
          type="datetimerange"
          format="MM-DD HH:mm"
          start-placeholder="开始时间"
          end-placeholder="结束时间"
          class="ctl w-[360px]!"
          :clearable="true"
        />
        <el-select
          :model-value="history.interval"
          class="ctl w-[130px]!"
          @update:model-value="setInterval"
        >
          <el-option v-for="item in HISTORY_INTERVALS" :key="item" :label="item" :value="item" />
        </el-select>
        <div class="h-[22px] w-px bg-line" />
        <span class="text-caption text-subtle">数据类型</span>
        <div class="flex items-center gap-1.5">
          <button
            v-for="kind in kindFilters"
            :key="kind.key"
            type="button"
            class="rounded-[5px] px-3 py-[6px] text-small transition-colors"
            :class="
              history.kindFilter === kind.key
                ? 'bg-brand text-white'
                : 'bg-raised text-muted hover:text-ink'
            "
            @click="setKind(kind.key)"
          >
            {{ kind.label }}
          </button>
        </div>
        <div class="flex-1" />
        <ActionBtn @click="history.resetFilters">重置</ActionBtn>
        <ActionBtn variant="primary" :loading="history.busy" @click="history.query">查询</ActionBtn>
      </div>

      <!-- 已选测点 -->
      <div class="flex flex-wrap items-center gap-2">
        <span class="text-caption text-muted">已选测点 {{ history.selected.length }} / 6</span>
        <span
          v-for="(point, index) in history.selected"
          :key="point.pointId"
          class="flex items-center gap-[7px] rounded-[4px] bg-raised py-1 pl-[9px] pr-2 text-caption text-ink"
        >
          <i class="dot" :style="{ backgroundColor: chipColor(index) }" />
          {{ point.deviceName }} · {{ point.name }}
          <button
            type="button"
            class="text-subtle hover:text-ink"
            aria-label="移除测点"
            @click="history.removePoint(point.pointId)"
          >
            <AppIcon name="x" :size="12" />
          </button>
        </span>
        <button
          v-if="history.selected.length"
          type="button"
          class="rounded-[4px] border border-line px-[9px] py-1 text-caption text-muted hover:text-ink"
          @click="history.clearSelection"
        >
          清空
        </button>
        <div class="flex-1" />
        <span class="text-caption text-subtle">最多同时对比 6 个测点</span>
      </div>

      <div v-if="history.error" class="flex items-center gap-2">
        <InfoBar :text="history.error" tone="danger" class="flex-1" />
        <ActionBtn :loading="history.loading" @click="history.load">重新加载目录</ActionBtn>
      </div>
      <InfoBar v-if="history.queryError" :text="history.queryError" tone="danger" />
      <InfoBar
        v-if="history.truncated"
        text="部分结果已截断（达到结果上限或扫描预算），请缩小时间范围或减少测点。CSV 仅包含当前已返回的数据。"
        tone="warn"
      />
      <InfoBar v-if="history.notice" :text="history.notice" tone="warn" />

      <!-- 趋势对比 -->
      <section class="panel flex min-h-[360px] flex-1 flex-col overflow-hidden">
        <div class="panel-bar">
          <div class="flex items-center gap-2.5">
            <span class="text-body font-semibold text-ink">趋势对比</span>
            <span class="text-caption text-subtle">{{ unitSummary }}</span>
          </div>
          <div class="flex items-center gap-4">
            <span
              v-for="(series, index) in history.chartSeries"
              :key="series.name"
              class="flex items-center gap-[6px] text-caption text-muted"
            >
              <i class="dot" :style="{ backgroundColor: series.color }" />{{
                legendName(history.visiblePoints[index]!.name, history.visiblePoints[index]!.unit)
              }}
            </span>
            <AppIcon name="maximize-2" :size="14" class="text-muted" />
          </div>
        </div>
        <div class="flex min-h-0 flex-1 flex-col p-4">
          <LineChart
            v-if="history.hasChartData"
            :series="history.chartSeries"
            :y-ticks="history.yTicks"
            :x-labels="history.xLabels"
          />
          <div v-else class="flex flex-1 items-center justify-center text-small text-muted">
            {{ emptyText }}
          </div>
        </div>
      </section>

      <!-- 数据明细 -->
      <section class="panel flex h-[380px] shrink-0 flex-col overflow-hidden">
        <div class="panel-bar">
          <div class="flex items-center gap-2.5">
            <span class="text-body font-semibold text-ink">数据明细</span>
            <span class="text-caption text-subtle"
              >采样间隔 {{ history.interval
              }}<template v-if="history.interval !== '原始数据'">
                · 每个时间桶取最后一条样本</template
              ></span
            >
          </div>
          <span class="text-caption text-subtle">{{ history.rangeText }}</span>
        </div>
        <div class="min-h-0 flex-1 overflow-auto">
          <div class="dt-head sticky top-0 z-10">
            <span class="dt-th dt-cell w-[200px]">时间</span>
            <span
              v-for="point in history.visiblePoints"
              :key="point.pointId"
              class="dt-th dt-cell flex-1 truncate"
            >
              {{ point.unit ? `${point.name} (${point.unit})` : point.name }}
            </span>
            <span class="dt-th dt-cell w-[80px]">质量</span>
          </div>
          <div v-for="row in history.pagedRows" :key="row.timestamp" class="dt-row h-[34px]">
            <span class="dt-cell w-[200px] text-muted">{{ row.time }}</span>
            <span
              v-for="(cell, index) in row.cells"
              :key="index"
              class="dt-cell flex-1 truncate"
              :class="cell.good ? 'text-ink' : 'text-subtle'"
            >
              {{ cell.text }}
            </span>
            <span class="dt-cell w-[80px]" :class="row.allGood ? 'text-ok' : 'text-warn'">
              {{ row.allGood ? '有效' : '部分无效' }}
            </span>
          </div>
          <div
            v-if="!history.tableRows.length"
            class="flex h-24 items-center justify-center text-small text-muted"
          >
            查询后在此展示各测点的历史采样明细。
          </div>
        </div>
        <el-pagination
          v-model:current-page="history.page"
          :page-size="history.pageSize"
          :total="history.tableRows.length"
          :pager-count="7"
          layout="total, prev, pager, next"
          class="shrink-0 px-4 py-2"
        />
      </section>
    </section>
  </div>
</template>
