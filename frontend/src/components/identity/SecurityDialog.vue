<script setup lang="ts">
import { reactive, ref, watch } from 'vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import type { SecurityPrompt } from '@/composables/useSecurityAction'
import { passwordError } from '@/domain/identity'

const props = defineProps<{
  open: boolean
  busy: boolean
  error: string
  blocked: boolean
  prompt: SecurityPrompt | null
}>()
const emit = defineEmits<{ confirm: [password: string, temporary: string]; cancel: [] }>()
const password = ref('')
const temporary = ref('')
const errors = reactive({ password: '', temporary: '' })
watch(
  () => props.open,
  () => {
    password.value = ''
    temporary.value = ''
    errors.password = ''
    errors.temporary = ''
  },
)
function submit() {
  errors.password =
    props.prompt?.password !== false && !password.value ? '请输入你当前账号的密码' : ''
  errors.temporary = props.prompt?.temporaryPassword
    ? passwordError(temporary.value, props.prompt.username ?? '', props.prompt.minimum)
    : ''
  if (!errors.password && !errors.temporary) emit('confirm', password.value, temporary.value)
}
</script>
<template>
  <el-dialog
    :model-value="open"
    width="min(540px, calc(100vw - 32px))"
    :title="prompt?.title"
    :close-on-click-modal="false"
    :close-on-press-escape="!busy"
    :show-close="!busy"
    class="identity-dialog"
    append-to-body
    @close="emit('cancel')"
  >
    <form v-if="prompt" class="flex flex-col gap-5" @submit.prevent="submit">
      <p v-if="prompt.target" class="text-[16px] font-medium text-ink">{{ prompt.target }}</p>
      <p class="text-[14px] leading-relaxed text-muted">{{ prompt.message }}</p>
      <FormField
        v-if="prompt.temporaryPassword"
        label="新临时密码"
        :error="errors.temporary"
        :hint="`${prompt.minimum ?? 12}–32 个字符，至少三类字符`"
        required
        v-slot="field"
        ><el-input
          :id="field.id"
          v-model="temporary"
          type="password"
          show-password
          autocomplete="new-password"
          placeholder="请输入临时密码"
          :disabled="busy"
      /></FormField>
      <FormField
        v-if="prompt.password !== false"
        label="管理员当前密码"
        :error="errors.password"
        hint="验证你自己的密码，不是目标用户的密码。"
        required
        v-slot="field"
        ><el-input
          :id="field.id"
          v-model="password"
          type="password"
          show-password
          autocomplete="current-password"
          placeholder="验证你自己的密码"
          :disabled="busy"
      /></FormField>
      <InfoBar v-if="error" tone="danger" :text="error" size="md" />
      <div class="mt-1 flex justify-end gap-3">
        <ActionBtn :disabled="busy" @click="emit('cancel')">{{
          blocked ? '关闭并刷新' : '取消'
        }}</ActionBtn
        ><ActionBtn
          type="submit"
          :variant="prompt.destructive ? 'danger' : 'primary'"
          :loading="busy"
          :disabled="blocked"
          ><AppIcon v-if="prompt.destructive" name="triangle-alert" :size="15" />{{
            prompt.confirmLabel ?? '确认'
          }}</ActionBtn
        >
      </div>
    </form>
  </el-dialog>
</template>
