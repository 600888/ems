<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import MetricCards, { type MetricCard } from '@/components/common/MetricCards.vue'
import PanelBox from '@/components/common/PanelBox.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import DeviceScopeTree from '@/components/device/DeviceScopeTree.vue'
import AlarmTable from '@/components/alarm/AlarmTable.vue'
import AlarmAckForm from '@/components/alarm/AlarmAckForm.vue'
import AlarmDetailDrawer from '@/components/alarm/AlarmDetailDrawer.vue'
import { useAlarmStore } from '@/stores/alarm'
import { useAuthStore } from '@/stores/auth'
import { useAlarmSound } from '@/composables/useAlarmSound'
import { ALARM_FRESH_MS, ALARM_POLL_MS, alarmRange, alarmTime, alarmsCsv } from '@/domain/alarm'
import type { AlarmFilters, AlarmView } from '@/types/alarm'
import type { DeviceNode } from '@/types/domain'

const alarm = useAlarmStore(),
  auth = useAuthStore(),
  route = useRoute(),
  router = useRouter()
const view = computed<AlarmView>(() => (route.query.view === 'history' ? 'history' : 'active'))
const scopeId = ref(''),
  keyword = ref(''),
  scopeOpen = ref(false),
  batchOpen = ref(false),
  batchRemark = ref('')
const draft = reactive<Pick<AlarmFilters, 'severity' | 'acknowledgement' | 'recovery' | 'keyword'>>(
  { severity: '', acknowledgement: '', recovery: '', keyword: '' },
)
const range = ref<[string, string] | null>(null),
  quick = ref<1 | 7 | 30 | null>(1),
  now = ref(Date.now())
const rows = computed(() => alarm.result?.items ?? [])
const isActive = computed(() => view.value === 'active')
const sound = useAlarmSound(rows, isActive)
const canAck = computed(() => auth.can('alarm.ack'))
const tree = computed(() => alarm.allowedTree.tree)
const stale = computed(() =>
  Boolean(alarm.receivedAt && (alarm.error || now.value - alarm.receivedAt > ALARM_FRESH_MS)),
)
const frozen = computed(
  () => alarm.paused || alarm.selectedIds.length > 0 || Boolean(alarm.detailId) || batchOpen.value,
)
const batchBlock = computed(() => {
  void now.value
  return alarm.confirmationBlock(alarm.pendingSelection.map((row) => row.alarm_id))
})
const summary = computed(() => alarm.result?.summary)
const metrics = computed<MetricCard[]>(() => {
  const data = summary.value
  return [
    {
      label: '活动告警',
      value: data?.active ?? '—',
      icon: 'bell-ring',
      note: '当前尚未恢复',
      tone: 'ink',
    },
    {
      label: '严重',
      value: data?.critical ?? '—',
      icon: 'octagon-alert',
      note: '优先检查设备保护',
      tone: 'danger',
    },
    {
      label: '警告',
      value: data?.warning ?? '—',
      icon: 'triangle-alert',
      note: '关注运行边界',
      tone: 'warn',
    },
    { label: '提示', value: data?.info ?? '—', icon: 'info', note: '运行事件提醒', tone: 'accent' },
    {
      label: '待确认',
      value: data?.unacknowledged ?? '—',
      icon: 'circle-check',
      note: '当前筛选结果中',
      tone: 'warn',
    },
  ]
})
function findNode(nodes: DeviceNode[], id: string): DeviceNode | undefined {
  for (const node of nodes) {
    if (node.id === id) return node
    const found = findNode(node.children ?? [], id)
    if (found) return found
  }
}
const scopeName = computed(() => findNode(tree.value, scopeId.value)?.name ?? '全部授权设备')
function pickRange(days: 1 | 7 | 30) {
  quick.value = days
  range.value = alarmRange(days).map((value) => alarmTime(value)) as [string, string]
}
function queryFilters(): AlarmFilters {
  if (view.value === 'history' && quick.value) pickRange(quick.value)
  return {
    ...draft,
    view: view.value,
    device_root_id: scopeId.value,
    recovery: view.value === 'active' ? 'active' : draft.recovery,
    ...(view.value === 'history'
      ? {
          start_ms: range.value
            ? Date.parse(range.value[0].replace(' ', 'T') + '+08:00')
            : undefined,
          end_ms: range.value ? Date.parse(range.value[1].replace(' ', 'T') + '+08:00') : undefined,
        }
      : {}),
  }
}
async function search() {
  await alarm.applyFilters(queryFilters())
}
function reset() {
  Object.assign(draft, { severity: '', acknowledgement: '', recovery: '', keyword: '' })
  pickRange(1)
  void search()
}
function selectScope(node?: DeviceNode) {
  if (alarm.ackBusy) return
  scopeId.value = node?.id ?? ''
  scopeOpen.value = false
  void search()
}
function changeView(value: string) {
  if (alarm.ackBusy) return
  void router.push({ query: value === 'history' ? { view: 'history' } : {} })
}
watch(view, () => {
  batchOpen.value = false
  alarm.selectedIds = []
  reset()
})
watch(
  () =>
    JSON.stringify([
      auth.user?.user_id,
      auth.session?.permissions,
      auth.session?.scope_mode,
      auth.session?.scopes,
    ]),
  () => {
    scopeId.value = ''
    batchOpen.value = false
    scopeOpen.value = false
    batchRemark.value = ''
    if (auth.can('alarm.read')) {
      void alarm.loadTree()
      reset()
    }
  },
)
function openBatch() {
  batchRemark.value = ''
  alarm.actionError = ''
  batchOpen.value = true
}
async function confirmBatch(remark: string) {
  if (
    await alarm.acknowledge(
      alarm.pendingSelection.map((row) => row.alarm_id),
      remark,
    )
  )
    batchOpen.value = false
}
async function refresh() {
  await alarm.query()
  if (alarm.detailId) await alarm.openDetail(alarm.detailId)
}
async function download() {
  const records = await alarm.exportRows()
  if (!records) return
  const url = URL.createObjectURL(
    new Blob([alarmsCsv(records)], { type: 'text/csv;charset=utf-8' }),
  )
  const link = document.createElement('a')
  link.href = url
  link.download = `告警记录-${alarmTime(Date.now()).slice(0, 10)}.csv`
  link.click()
  window.setTimeout(() => URL.revokeObjectURL(url), 0)
}
let timer = 0,
  lastAttempt = 0
function tick() {
  now.value = Date.now()
  if (
    !auth.can('alarm.read') ||
    !isActive.value ||
    frozen.value ||
    alarm.loading ||
    alarm.ackBusy ||
    document.visibilityState !== 'visible'
  )
    return
  if (now.value - lastAttempt < ALARM_POLL_MS) return
  lastAttempt = now.value
  void alarm.query()
}
onMounted(() => {
  pickRange(1)
  lastAttempt = Date.now()
  void alarm.loadTree()
  void search()
  timer = window.setInterval(tick, 1000)
})
onBeforeUnmount(() => {
  window.clearInterval(timer)
  alarm.deactivate()
})
</script>

<template>
  <div class="flush-workspace">
    <DeviceScopeTree
      v-model:keyword="keyword"
      :tree="tree"
      :model-value="scopeId"
      title="设备范围"
      hint="包含下级设备 · 数字为活动告警"
      :counts="alarm.result?.device_counts"
      :width="264"
      class="hidden! lg:flex!"
      @select="selectScope"
    >
      <template #toolbar
        ><button
          type="button"
          class="self-start rounded bg-sel px-2 py-1 text-small text-accent"
          :disabled="alarm.ackBusy"
          @click="selectScope()"
        >
          全部授权设备
        </button></template
      >
      <template #footer
        ><p class="text-small text-ink">{{ scopeName }}</p>
        <p class="text-caption text-subtle">仅展示授权设备范围内的告警</p>
        <ActionBtn
          v-if="alarm.treeError"
          size="sm"
          :loading="alarm.treeLoading"
          @click="alarm.loadTree"
          >重新加载设备目录</ActionBtn
        ></template
      >
    </DeviceScopeTree>
    <section
      class="flex min-h-0 min-w-0 flex-1 flex-col gap-3 overflow-y-auto px-3 pt-4 pb-5 sm:px-5"
    >
      <header class="flex shrink-0 flex-wrap items-center gap-3 py-1">
        <AppIcon name="bell-ring" :size="24" class="text-accent" />
        <h1 class="text-title font-semibold">告警中心</h1>
        <span class="hidden text-small text-muted xl:block">{{ scopeName }}</span>
        <div class="flex-1" />
        <ActionBtn
          v-if="isActive"
          size="sm"
          :icon="sound.enabled.value ? 'volume-2' : 'volume-x'"
          :aria-pressed="sound.enabled.value"
          @click="sound.toggle"
          >{{ sound.enabled.value ? '声音开启' : '声音关闭' }}</ActionBtn
        >
        <ActionBtn
          v-else
          size="sm"
          icon="download"
          :loading="alarm.exporting"
          :disabled="!alarm.result?.total || Boolean(alarm.error) || alarm.loading"
          @click="download"
          >导出查询结果</ActionBtn
        >
        <ActionBtn
          size="sm"
          icon="refresh-cw"
          :loading="alarm.loading"
          :disabled="alarm.ackBusy"
          @click="refresh"
          >刷新</ActionBtn
        >
      </header>
      <ActionBtn size="sm" icon="list-tree" class="self-start lg:hidden!" @click="scopeOpen = true"
        >设备范围 · {{ scopeName }}</ActionBtn
      >
      <InfoBar
        v-if="alarm.treeError"
        :text="`设备目录加载失败：${alarm.treeError}。告警查询仍按服务端授权范围进行。`"
        tone="warn"
      />
      <InfoBar v-if="sound.error.value" :text="sound.error.value" tone="warn" />
      <MetricCards v-if="isActive" :items="metrics" inline />
      <UnderlineTabs
        :model-value="view"
        :height="42"
        :items="[
          { key: 'active', label: '实时告警' },
          { key: 'history', label: '历史告警' },
        ]"
        @update:model-value="changeView"
      >
        <template #actions
          ><span class="hidden text-caption text-subtle sm:block"
            >时间均为北京时间 UTC+8</span
          ></template
        >
      </UnderlineTabs>
      <form class="panel flex shrink-0 flex-col gap-3 p-4" @submit.prevent="search">
        <div v-if="!isActive" class="flex flex-wrap items-center gap-2.5">
          <label class="text-small text-muted">发生时间（UTC+8）</label>
          <el-date-picker
            v-model="range"
            type="datetimerange"
            format="YYYY-MM-DD HH:mm:ss"
            value-format="YYYY-MM-DD HH:mm:ss"
            start-placeholder="开始时间"
            end-placeholder="结束时间"
            class="ctl max-w-full! w-[410px]!"
            style="flex: 0 1 430px"
            :disabled="alarm.ackBusy"
            @change="quick = null"
          />
          <button
            v-for="item in [
              { days: 1, label: '今天' },
              { days: 7, label: '近 7 天' },
              { days: 30, label: '近 30 天' },
            ] as const"
            :key="item.days"
            type="button"
            class="rounded px-3 py-1.5 text-small"
            :class="quick === item.days ? 'bg-sel text-accent' : 'bg-raised text-muted'"
            :disabled="alarm.ackBusy"
            @click="pickRange(item.days)"
          >
            {{ item.label }}
          </button>
          <span class="ml-auto text-caption text-subtle">单次查询最长 31 天</span>
        </div>
        <div class="flex flex-wrap items-center gap-3">
          <el-select
            v-model="draft.severity"
            placeholder="全部级别"
            aria-label="告警级别"
            class="ctl w-[130px]!"
            :disabled="alarm.ackBusy"
            ><el-option label="全部级别" value="" /><el-option
              label="严重"
              value="critical" /><el-option label="警告" value="warning" /><el-option
              label="提示"
              value="info"
          /></el-select>
          <el-select
            v-if="!isActive"
            v-model="draft.recovery"
            placeholder="全部恢复状态"
            aria-label="恢复状态"
            class="ctl w-[150px]!"
            :disabled="alarm.ackBusy"
            ><el-option label="全部恢复状态" value="" /><el-option
              label="未恢复"
              value="active" /><el-option label="已恢复" value="cleared"
          /></el-select>
          <el-select
            v-model="draft.acknowledgement"
            placeholder="全部确认状态"
            aria-label="确认状态"
            class="ctl w-[150px]!"
            :disabled="alarm.ackBusy"
            ><el-option label="全部确认状态" value="" /><el-option
              label="未确认"
              value="pending" /><el-option label="已确认" value="acknowledged"
          /></el-select>
          <el-input
            v-model="draft.keyword"
            class="ctl w-full! sm:w-[300px]!"
            aria-label="告警关键词"
            placeholder="搜索告警名称 / 编码 / 设备"
            clearable
            :disabled="alarm.ackBusy"
            ><template #prefix><AppIcon name="search" :size="14" class="text-subtle" /></template
          ></el-input>
          <ActionBtn
            type="submit"
            size="sm"
            variant="primary"
            icon="search"
            :loading="alarm.loading"
            :disabled="alarm.ackBusy"
            >查询</ActionBtn
          ><ActionBtn size="sm" variant="ghost" :disabled="alarm.ackBusy" @click="reset"
            >重置</ActionBtn
          >
          <span class="ml-auto text-caption text-subtle">发生时间 · 最新优先</span>
        </div>
      </form>
      <InfoBar v-if="alarm.notice" :text="alarm.notice" :tone="alarm.noticeTone" />
      <InfoBar
        v-if="alarm.result && alarm.archivePending"
        :text="`还有 ${alarm.archivePending} 条告警事件待归档。`"
        tone="info"
      />
      <div v-if="alarm.unresolvedIds.length" class="flex shrink-0 flex-wrap items-center gap-2">
        <InfoBar
          class="flex-1"
          tone="warn"
          text="上次告警确认结果尚未核实，请先刷新确认结果。已填写的备注会保留。"
        />
        <ActionBtn size="sm" :loading="alarm.resolving" @click="alarm.resolveAcknowledgements"
          >刷新确认结果</ActionBtn
        >
      </div>
      <InfoBar v-if="!canAck" text="当前账号仅可查看告警，无确认权限。" icon="lock-keyhole" />
      <div
        v-if="isActive && alarm.result"
        class="flex shrink-0 flex-wrap items-center gap-3 rounded-md px-3 py-2 text-small"
        :class="stale ? 'bg-warnbox text-warn' : 'bg-okbox text-ok'"
        role="status"
      >
        <AppIcon :name="stale ? 'wifi-off' : frozen ? 'pause' : 'radio'" :size="15" />
        <span>{{ stale ? '告警状态可能已变化' : frozen ? '列表重排已暂停' : '实时更新中' }}</span>
        <span class="text-caption text-muted"
          >最近更新 {{ alarmTime(alarm.receivedAt, true)
          }}<template v-if="stale">
            · {{ Math.floor((now - alarm.receivedAt) / 1000) }} 秒未更新</template
          ></span
        >
        <span class="ml-auto hidden text-caption text-muted xl:block"
          >选中记录或打开详情时暂停列表重排</span
        >
        <ActionBtn
          size="sm"
          :icon="alarm.paused ? 'radio' : 'pause'"
          @click="alarm.paused = !alarm.paused"
          >{{ alarm.paused ? '继续更新' : '暂停更新' }}</ActionBtn
        >
      </div>
      <div v-if="alarm.error && alarm.result" class="flex shrink-0 flex-wrap items-center gap-2">
        <InfoBar
          class="flex-1"
          tone="warn"
          :text="`${alarm.error} 当前保留上次查询结果，请刷新后再确认。`"
        /><ActionBtn size="sm" :loading="alarm.loading" @click="refresh">重试</ActionBtn>
      </div>
      <div
        v-if="!isActive && summary"
        class="panel flex shrink-0 flex-wrap items-center gap-x-7 gap-y-3 px-4 py-3"
      >
        <span
          v-for="item in [
            { label: '发生告警', value: summary.total, tone: 'text-ink' },
            { label: '已恢复', value: summary.cleared, tone: 'text-ok' },
            { label: '未恢复', value: summary.active, tone: 'text-danger' },
            { label: '待确认', value: summary.unacknowledged, tone: 'text-warn' },
          ]"
          :key="item.label"
          class="flex items-center gap-3 text-small text-muted"
          >{{ item.label
          }}<strong class="text-section tabular-nums" :class="item.tone">{{
            item.value
          }}</strong></span
        >
        <span class="ml-auto text-caption text-subtle"
          >待确认包含已恢复告警 · 统计当前查询结果</span
        >
      </div>
      <AsyncContent
        :loading="alarm.loading && !alarm.result"
        :error="!alarm.result ? alarm.error : ''"
        @retry="search"
      >
        <PanelBox
          :title="isActive ? '活动告警' : '告警记录'"
          :hint="`共 ${alarm.result?.total ?? 0} 条`"
          flush
          class="shrink-0"
        >
          <template #actions>
            <template v-if="isActive && canAck"
              ><span class="hidden text-caption text-subtle xl:block">{{
                alarm.selectedIds.length
                  ? `已选 ${alarm.selectedIds.length} 条`
                  : '选中后可批量确认'
              }}</span
              ><ActionBtn
                size="sm"
                icon="list-checks"
                :disabled="!alarm.pendingSelection.length || alarm.ackBusy"
                disabled-reason="请选择尚未确认的告警"
                @click="openBatch"
                >批量确认</ActionBtn
              ></template
            >
            <span v-else class="hidden text-caption text-subtle lg:block"
              >查询于 {{ alarmTime(alarm.receivedAt, true) }}</span
            >
          </template>
          <AlarmTable
            v-if="rows.length"
            :rows="rows"
            :history="!isActive"
            :selected="alarm.selectedIds"
            :now="now"
            :can-ack="canAck"
            :disabled="alarm.ackBusy"
            @select="alarm.selectedIds = $event"
            @detail="alarm.openDetail"
          />
          <EmptyState
            v-else
            :title="
              isActive && !draft.keyword && !draft.severity && !draft.acknowledgement
                ? '当前范围无活动告警'
                : '没有符合条件的告警'
            "
            :desc="
              isActive
                ? '可调整设备范围或在历史告警中查看已恢复记录。'
                : '请调整发生时间、设备范围或筛选条件后重新查询。'
            "
            icon="shield-check"
            class="m-4"
            ><ActionBtn size="sm" @click="reset">清空筛选条件</ActionBtn></EmptyState
          >
          <div
            class="flex flex-wrap items-center justify-between gap-2 border-t border-line px-4 py-3"
          >
            <span class="text-caption text-subtle">{{
              alarm.result?.total
                ? `显示 ${(alarm.page - 1) * alarm.pageSize + 1}–${Math.min(alarm.page * alarm.pageSize, alarm.result.total)} 条`
                : '无记录'
            }}</span>
            <el-pagination
              :current-page="alarm.page"
              :page-size="alarm.pageSize"
              :total="alarm.result?.total ?? 0"
              :page-sizes="[8, 20, 50]"
              :pager-count="5"
              layout="sizes, prev, pager, next"
              :disabled="alarm.loading || alarm.ackBusy"
              @update:current-page="alarm.paginate($event)"
              @update:page-size="alarm.paginate(1, $event)"
            />
          </div>
        </PanelBox>
      </AsyncContent>
    </section>
    <AlarmDetailDrawer :now="now" />
    <el-drawer
      v-model="scopeOpen"
      title="设备范围"
      direction="ltr"
      size="min(320px, 100vw)"
      class="ems-drawer"
      append-to-body
      ><DeviceScopeTree
        v-model:keyword="keyword"
        :tree="tree"
        :model-value="scopeId"
        title="选择设备范围"
        :counts="alarm.result?.device_counts"
        class="h-full w-full!"
        @select="selectScope"
        ><template #toolbar
          ><ActionBtn size="sm" @click="selectScope()">全部授权设备</ActionBtn></template
        ></DeviceScopeTree
      ></el-drawer
    >
    <el-dialog
      v-model="batchOpen"
      title="批量确认告警"
      width="min(580px, calc(100vw - 24px))"
      :close-on-click-modal="false"
      :close-on-press-escape="!alarm.ackBusy"
      :show-close="!alarm.ackBusy"
      append-to-body
    >
      <div class="flex flex-col gap-4">
        <InfoBar
          tone="warn"
          :text="`已选择 ${alarm.selection.length} 条告警，${alarm.pendingSelection.length} 条可确认，${alarm.selection.length - alarm.pendingSelection.length} 条已确认将跳过。`"
        />
        <ul class="max-h-48 overflow-y-auto">
          <li
            v-for="item in alarm.selection"
            :key="item.alarm_id"
            class="flex gap-4 border-b border-line py-2 text-small"
          >
            <span class="min-w-0 flex-1">{{ item.name }}</span
            ><span :class="item.acknowledgement ? 'text-muted' : 'text-warn'">{{
              item.acknowledgement ? '已确认 · 跳过' : '待确认'
            }}</span>
          </li>
        </ul>
        <AlarmAckForm
          v-model="batchRemark"
          :label="`确认 ${alarm.pendingSelection.length} 条告警`"
          :busy="alarm.ackBusy || alarm.resolving"
          :error="alarm.actionError"
          :disabled-reason="batchBlock"
          :needs-resolution="Boolean(alarm.unresolvedIds.length)"
          @submit="confirmBatch"
          @refresh="alarm.unresolvedIds.length ? alarm.resolveAcknowledgements() : refresh()"
          ><template #actions
            ><ActionBtn size="sm" :disabled="alarm.ackBusy" @click="batchOpen = false"
              >取消</ActionBtn
            ></template
          ></AlarmAckForm
        >
      </div>
    </el-dialog>
  </div>
</template>
