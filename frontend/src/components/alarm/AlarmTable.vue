<script setup lang="ts">
import { computed } from 'vue'
import StatusTag from '@/components/common/StatusTag.vue'
import { alarmDuration, alarmLevels, alarmTime } from '@/domain/alarm'
import type { AlarmRecord } from '@/types/alarm'

const props = defineProps<{
  rows: AlarmRecord[]
  history: boolean
  selected: string[]
  now: number
  canAck: boolean
  disabled?: boolean
}>()
const emit = defineEmits<{ select: [ids: string[]]; detail: [id: string] }>()
const all = computed(
  () => props.rows.length > 0 && props.rows.every((row) => props.selected.includes(row.alarm_id)),
)
const some = computed(() => props.rows.some((row) => props.selected.includes(row.alarm_id)))
function toggle(id: string, checked: boolean) {
  emit(
    'select',
    checked
      ? [...new Set([...props.selected, id])]
      : props.selected.filter((value) => value !== id),
  )
}
</script>

<template>
  <div class="hidden overflow-x-auto md:block">
    <table class="alarm-table w-full" :class="history ? 'min-w-[1360px]' : 'min-w-[1150px]'">
      <caption class="sr-only">
        {{
          history ? '历史告警记录' : '当前活动告警'
        }}
      </caption>
      <thead>
        <tr>
          <th v-if="!history && canAck" scope="col" class="w-11">
            <input
              type="checkbox"
              aria-label="选择本页全部告警"
              :checked="all"
              :indeterminate="some && !all"
              :disabled="disabled"
              @change="emit('select', all ? [] : rows.map((row) => row.alarm_id))"
            />
          </th>
          <th scope="col" class="w-20">级别</th>
          <th scope="col">告警名称 / {{ history ? '编码' : '触发值' }}</th>
          <th scope="col" class="w-[200px]">所属设备</th>
          <th scope="col" class="w-[166px]">发生时间</th>
          <th v-if="history" scope="col" class="w-[152px]">恢复时间</th>
          <th scope="col" class="w-[108px]">持续时长</th>
          <th scope="col" class="w-[92px]">确认状态</th>
          <th v-if="history" scope="col" class="w-[130px]">确认人 / 时间</th>
          <th scope="col" class="w-[116px]">操作</th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="row in rows"
          :key="row.alarm_id"
          :class="selected.includes(row.alarm_id) && 'is-selected'"
        >
          <td v-if="!history && canAck">
            <input
              type="checkbox"
              :aria-label="`选择${row.name}`"
              :checked="selected.includes(row.alarm_id)"
              :disabled="disabled"
              @change="toggle(row.alarm_id, !selected.includes(row.alarm_id))"
            />
          </td>
          <td>
            <StatusTag
              :tone="alarmLevels[row.severity].tone"
              :label="alarmLevels[row.severity].label"
              :dot="false"
            />
          </td>
          <td class="min-w-[250px]">
            <p class="cell-main font-medium">{{ row.name }}</p>
            <p class="cell-sub mt-1">{{ history ? row.alarm_code : row.trigger_summary || '—' }}</p>
          </td>
          <td>
            <p class="cell-main">{{ row.device_name }}</p>
            <p class="cell-sub mt-1" :title="row.device_path">{{ row.device_path }}</p>
          </td>
          <td class="tabular-nums text-muted">{{ alarmTime(row.occurred_at_ms) }}</td>
          <td
            v-if="history"
            class="tabular-nums"
            :class="row.cleared_at_ms == null ? 'text-danger' : 'text-ok'"
          >
            {{ row.cleared_at_ms == null ? '未恢复' : alarmTime(row.cleared_at_ms) }}
          </td>
          <td class="tabular-nums text-muted">{{ alarmDuration(row, now) }}</td>
          <td :class="row.acknowledgement ? 'text-muted' : 'text-warn'">
            {{ row.acknowledgement ? '已确认' : '未确认' }}
          </td>
          <td v-if="history" class="text-muted">
            <p>{{ row.acknowledgement?.display_name || '—' }}</p>
            <p v-if="row.acknowledgement" class="cell-sub mt-1">
              {{ alarmTime(row.acknowledgement.at_ms) }}
            </p>
          </td>
          <td>
            <div class="flex items-center gap-3">
              <button
                type="button"
                class="link-action"
                :disabled="disabled"
                :aria-label="`查看${row.name}详情`"
                @click="emit('detail', row.alarm_id)"
              >
                详情
              </button>
              <button
                v-if="!history && canAck && !row.acknowledgement"
                type="button"
                class="link-action"
                :disabled="disabled"
                :aria-label="`确认${row.name}`"
                @click="emit('detail', row.alarm_id)"
              >
                确认
              </button>
            </div>
          </td>
        </tr>
      </tbody>
    </table>
  </div>
  <div class="divide-y divide-line md:hidden">
    <article
      v-for="row in rows"
      :key="row.alarm_id"
      class="flex flex-col gap-3 p-4"
      :class="selected.includes(row.alarm_id) && 'bg-selrow'"
    >
      <div class="flex items-center gap-2">
        <input
          v-if="!history && canAck"
          type="checkbox"
          :aria-label="`选择${row.name}`"
          :checked="selected.includes(row.alarm_id)"
          :disabled="disabled"
          @change="toggle(row.alarm_id, !selected.includes(row.alarm_id))"
        />
        <StatusTag
          :tone="alarmLevels[row.severity].tone"
          :label="alarmLevels[row.severity].label"
          :dot="false"
        />
        <span
          class="ml-auto text-small"
          :class="row.acknowledgement ? 'text-muted' : 'text-warn'"
          >{{ row.acknowledgement ? '已确认' : '未确认' }}</span
        >
      </div>
      <h3 class="text-body font-medium">{{ row.name }}</h3>
      <p class="text-small text-muted">{{ row.trigger_summary }}</p>
      <p class="text-caption text-subtle">{{ row.device_path }} / {{ row.device_name }}</p>
      <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1 text-small">
        <dt class="text-muted">发生时间</dt>
        <dd>{{ alarmTime(row.occurred_at_ms) }}</dd>
        <dt class="text-muted">持续时长</dt>
        <dd>{{ alarmDuration(row, now) }}</dd>
        <template v-if="history"
          ><dt class="text-muted">恢复时间</dt>
          <dd :class="row.cleared_at_ms == null ? 'text-danger' : 'text-ok'">
            {{ row.cleared_at_ms == null ? '未恢复' : alarmTime(row.cleared_at_ms) }}
          </dd></template
        >
      </dl>
      <button
        type="button"
        class="link-action self-end"
        :disabled="disabled"
        @click="emit('detail', row.alarm_id)"
      >
        查看详情{{ canAck && !row.acknowledgement ? '与确认' : '' }}
      </button>
    </article>
  </div>
</template>

<style scoped>
.alarm-table {
  border-collapse: collapse;
  font-size: var(--font-size-table);
  line-height: var(--line-height-table);
}
.alarm-table th {
  height: 42px;
  background: var(--c-thead);
  color: var(--c-muted);
  font-size: var(--font-size-table-head);
  font-weight: 500;
  text-align: left;
}
.alarm-table th,
.alarm-table td {
  padding: 10px 12px;
  border-bottom: 1px solid var(--c-line);
}
.alarm-table td {
  height: 64px;
  overflow-wrap: anywhere;
}
.alarm-table tr:nth-child(even) {
  background: var(--c-rowalt);
}
.alarm-table tr.is-selected {
  background: var(--c-selrow);
}
input[type='checkbox'] {
  accent-color: var(--c-brand);
  width: 15px;
  height: 15px;
  cursor: pointer;
}
</style>
