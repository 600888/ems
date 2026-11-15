<script setup lang="ts">
import { ref, watch } from 'vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { alarmRemarkError } from '@/domain/alarm'

defineProps<{
  busy: boolean
  error?: string
  disabledReason?: string
  needsResolution?: boolean
  label?: string
}>()
const remark = defineModel<string>({ default: '' })
const emit = defineEmits<{ submit: [remark: string]; refresh: [] }>()
const validation = ref('')
watch(remark, () => {
  if (validation.value) validation.value = alarmRemarkError(remark.value)
})
function submit() {
  validation.value = alarmRemarkError(remark.value)
  if (!validation.value) emit('submit', remark.value.trim())
}
</script>

<template>
  <form class="flex flex-col gap-3" @submit.prevent="submit">
    <FormField
      label="确认备注"
      required
      :error="validation"
      hint="确认表示已知悉；设备恢复后才会解除告警。"
      v-slot="field"
    >
      <el-input
        :id="field.id"
        v-model="remark"
        type="textarea"
        :rows="3"
        maxlength="500"
        show-word-limit
        :disabled="busy"
        :aria-invalid="field.invalid"
        :aria-describedby="field.descriptionId"
        placeholder="请输入确认原因或现场处置情况"
      />
    </FormField>
    <InfoBar v-if="error" :text="error" tone="warn" />
    <div class="flex flex-wrap items-center justify-end gap-2">
      <slot name="actions" />
      <ActionBtn
        v-if="needsResolution"
        size="sm"
        :loading="busy"
        icon="refresh-cw"
        @click="emit('refresh')"
        >刷新确认结果</ActionBtn
      >
      <ActionBtn
        v-else-if="disabledReason && !busy"
        size="sm"
        icon="refresh-cw"
        @click="emit('refresh')"
        >刷新状态</ActionBtn
      >
      <ActionBtn
        type="submit"
        variant="primary"
        size="sm"
        icon="check"
        :loading="busy"
        :disabled="Boolean(disabledReason)"
        :disabled-reason="disabledReason"
        >{{ label || '确认告警' }}</ActionBtn
      >
    </div>
  </form>
</template>
