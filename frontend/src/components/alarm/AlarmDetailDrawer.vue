<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import AlarmAckForm from './AlarmAckForm.vue'
import { useAlarmStore } from '@/stores/alarm'
import { useAuthStore } from '@/stores/auth'
import { useMonitorStore } from '@/stores/monitor'
import { alarmDuration, alarmLevels, alarmTime } from '@/domain/alarm'

const props = defineProps<{ now: number }>()
const alarm = useAlarmStore(),
  auth = useAuthStore(),
  router = useRouter()
const remark = ref(''),
  navigationBusy = ref(false),
  navigationError = ref('')
const row = computed(() => alarm.detail)
const position = computed(
  () => alarm.result?.items.findIndex((item) => item.alarm_id === alarm.detailId) ?? -1,
)
const events = computed(() => [...(row.value?.events ?? [])].sort((a, b) => a.at_ms - b.at_ms))
const blocked = computed(() => {
  void props.now
  return alarm.confirmationBlock(alarm.detailId ? [alarm.detailId] : [])
})
watch(
  () => alarm.detailId,
  () => {
    remark.value = ''
    alarm.actionError = ''
    navigationError.value = ''
  },
)
function navigate(offset: number) {
  const next = alarm.result?.items[position.value + offset]
  if (next) void alarm.openDetail(next.alarm_id)
}
async function refresh() {
  if (alarm.unresolvedIds.length) await alarm.resolveAcknowledgements()
  else {
    await alarm.query()
    await alarm.openDetail(alarm.detailId)
  }
}
async function viewDevice() {
  if (!row.value) return
  navigationBusy.value = true
  navigationError.value = ''
  try {
    await useMonitorStore().selectDevice(row.value.device_id)
    await router.push('/monitor/telemetry')
  } catch {
    navigationError.value = '无法定位设备，请刷新后重试'
  } finally {
    navigationBusy.value = false
  }
}
function viewHistory() {
  if (!row.value?.point_id) return
  void router.push({
    name: 'history',
    query: {
      device_id: row.value.device_id,
      point_id: row.value.point_id,
      at_ms: row.value.occurred_at_ms,
    },
  })
}
const eventLabels = { occurred: '告警发生', cleared: '设备恢复', acknowledged: '告警确认' }
const qualityLabels = { good: '有效', stale: '数据陈旧', invalid: '无效', missing: '缺测' }
</script>

<template>
  <el-drawer
    :model-value="Boolean(alarm.detailId)"
    :with-header="false"
    size="min(700px, 100vw)"
    class="ems-drawer"
    append-to-body
    :close-on-click-modal="false"
    :close-on-press-escape="!alarm.ackBusy"
    :before-close="
      (done: () => void) => {
        if (!alarm.ackBusy) {
          alarm.closeDetail()
          done()
        }
      }
    "
    aria-label="告警详情"
  >
    <div class="flex h-full flex-col">
      <header class="flex min-h-16 shrink-0 items-center gap-3 border-b border-line px-5 sm:px-7">
        <AppIcon name="bell-ring" :size="20" class="text-accent" />
        <h2 class="text-section font-semibold">告警详情</h2>
        <div class="flex-1" />
        <button
          type="button"
          class="text-small text-muted disabled:opacity-40"
          :disabled="position <= 0 || alarm.ackBusy"
          @click="navigate(-1)"
        >
          上一条
        </button>
        <button
          type="button"
          class="text-small text-accent disabled:opacity-40"
          :disabled="
            position < 0 || position >= (alarm.result?.items.length ?? 0) - 1 || alarm.ackBusy
          "
          @click="navigate(1)"
        >
          下一条
        </button>
        <button
          type="button"
          aria-label="关闭告警详情"
          :disabled="alarm.ackBusy"
          @click="alarm.closeDetail"
        >
          <AppIcon name="x" :size="20" />
        </button>
      </header>
      <div class="flex min-h-0 flex-1 flex-col gap-5 overflow-y-auto px-5 py-6 sm:px-7">
        <AsyncContent
          :loading="alarm.detailLoading"
          :error="alarm.detailError"
          @retry="alarm.openDetail(alarm.detailId)"
        >
          <template v-if="row">
            <div class="flex flex-wrap gap-2">
              <StatusTag
                :tone="alarmLevels[row.severity].tone"
                :label="alarmLevels[row.severity].label"
                :dot="false"
              /><StatusTag
                :tone="row.cleared_at_ms == null ? 'danger' : 'ok'"
                :label="row.cleared_at_ms == null ? '未恢复' : '已恢复'"
                :dot="false"
              /><StatusTag
                :tone="row.acknowledgement ? 'mute' : 'warn'"
                :label="row.acknowledgement ? '已确认' : '未确认'"
                :dot="false"
              />
            </div>
            <div>
              <h3 class="text-title font-semibold">{{ row.name }}</h3>
              <p class="mt-2 text-caption break-all text-muted">告警编号 {{ row.alarm_id }}</p>
            </div>
            <InfoBar
              :tone="row.cleared_at_ms == null ? 'warn' : 'ok'"
              :text="
                row.cleared_at_ms == null
                  ? '确认仅表示已知悉，设备恢复后告警才会解除。'
                  : '设备已满足恢复条件；确认状态与设备恢复状态分别记录。'
              "
            />
            <dl class="grid grid-cols-[84px_1fr] gap-x-4 gap-y-3 text-small">
              <dt class="text-muted">所属设备</dt>
              <dd>{{ row.device_name }}</dd>
              <dt class="text-muted">设备路径</dt>
              <dd class="break-words">{{ row.device_path }}</dd>
              <dt class="text-muted">关联测点</dt>
              <dd>{{ row.point_name || '—' }}</dd>
              <dt class="text-muted">发生时间</dt>
              <dd>{{ alarmTime(row.occurred_at_ms) }}</dd>
              <dt class="text-muted">恢复时间</dt>
              <dd :class="row.cleared_at_ms == null ? 'text-danger' : 'text-ok'">
                {{ row.cleared_at_ms == null ? '尚未恢复' : alarmTime(row.cleared_at_ms) }}
              </dd>
              <dt class="text-muted">持续时长</dt>
              <dd>{{ alarmDuration(row, now) }}</dd>
            </dl>
            <section class="flex flex-col gap-3 rounded-md border border-line bg-surface p-4">
              <div class="flex flex-wrap items-center gap-2">
                <h4 class="text-body font-semibold">触发与恢复条件</h4>
                <span class="ml-auto text-caption break-all text-subtle">{{ row.alarm_code }}</span>
              </div>
              <div class="grid grid-cols-3 gap-3">
                <div>
                  <p class="text-caption text-muted">发生值</p>
                  <p class="mt-1 text-section font-semibold text-danger">
                    {{ row.trigger_value || '—' }}
                  </p>
                </div>
                <div>
                  <p class="text-caption text-muted">最新值</p>
                  <p
                    class="mt-1 text-section font-semibold"
                    :class="row.quality === 'good' ? 'text-ink' : 'text-subtle'"
                  >
                    {{ row.quality === 'good' ? row.current_value || '—' : '—' }}
                  </p>
                </div>
                <div>
                  <p class="text-caption text-muted">数据质量</p>
                  <p
                    class="mt-1 text-body"
                    :class="row.quality === 'good' ? 'text-ok' : 'text-warn'"
                  >
                    {{ row.quality ? qualityLabels[row.quality] : '未知' }}
                  </p>
                </div>
              </div>
              <p class="text-small text-muted">
                触发：{{ row.trigger_condition || row.trigger_summary || '—' }}
              </p>
              <p class="text-small text-muted">恢复：{{ row.recovery_condition || '—' }}</p>
              <p class="text-caption text-subtle">最新采样 {{ alarmTime(row.sampled_at_ms) }}</p>
            </section>
            <section>
              <h4 class="mb-4 text-body font-semibold">生命周期</h4>
              <ol class="flex flex-col gap-4">
                <li v-for="event in events" :key="event.event_id" class="flex gap-3">
                  <AppIcon
                    :name="event.kind === 'occurred' ? 'circle-alert' : 'circle-check'"
                    :size="18"
                    :class="event.kind === 'occurred' ? 'text-danger' : 'text-ok'"
                  />
                  <div>
                    <p class="text-small">
                      {{ alarmTime(event.at_ms) }} · {{ eventLabels[event.kind]
                      }}<span v-if="event.display_name"> · {{ event.display_name }}</span>
                    </p>
                    <p class="mt-1 whitespace-pre-wrap break-words text-small text-muted">
                      {{ event.detail }}
                    </p>
                  </div>
                </li>
                <li v-if="row.cleared_at_ms == null" class="flex gap-3">
                  <AppIcon name="clock-3" :size="18" class="text-subtle" />
                  <p class="text-small text-muted">等待设备满足恢复条件</p>
                </li>
              </ol>
            </section>
            <section v-if="row.acknowledgement" class="rounded-md bg-surface p-4">
              <h4 class="text-body font-semibold">确认记录</h4>
              <p class="mt-2 text-small text-muted">
                {{ row.acknowledgement.display_name }} · {{ alarmTime(row.acknowledgement.at_ms) }}
              </p>
              <p class="mt-2 whitespace-pre-wrap break-words text-body">
                {{ row.acknowledgement.remark }}
              </p>
            </section>
            <InfoBar
              v-if="!auth.can('alarm.ack')"
              text="当前账号为只读权限，可查看告警与生命周期，无确认权限。"
              icon="lock-keyhole"
            />
            <InfoBar v-if="navigationError" :text="navigationError" tone="danger" />
            <div class="mt-auto flex flex-wrap gap-2">
              <ActionBtn
                v-if="auth.can('monitor.read')"
                size="sm"
                icon="arrow-up-right"
                :loading="navigationBusy"
                @click="viewDevice"
                >查看设备</ActionBtn
              >
              <ActionBtn
                v-if="auth.can('history.read')"
                size="sm"
                icon="chart-no-axes-combined"
                :disabled="!row.point_id"
                disabled-reason="该告警未关联可查询的测点"
                @click="viewHistory"
                >历史趋势</ActionBtn
              >
              <ActionBtn size="sm" icon="refresh-cw" :disabled="alarm.ackBusy" @click="refresh"
                >刷新详情</ActionBtn
              >
            </div>
            <AlarmAckForm
              v-if="auth.can('alarm.ack') && !row.acknowledgement"
              v-model="remark"
              class="border-t border-line pt-5"
              :busy="alarm.ackBusy || alarm.resolving"
              :error="alarm.actionError"
              :disabled-reason="blocked"
              :needs-resolution="Boolean(alarm.unresolvedIds.length)"
              @submit="alarm.acknowledge([row.alarm_id], $event)"
              @refresh="refresh"
            />
            <InfoBar
              v-if="alarm.actionError && row.acknowledgement"
              :text="alarm.actionError"
              tone="warn"
            />
          </template>
        </AsyncContent>
      </div>
    </div>
  </el-drawer>
</template>
