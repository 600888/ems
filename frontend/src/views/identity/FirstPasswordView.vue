<script setup lang="ts">
import { onBeforeUnmount, reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/common/AppIcon.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import PasswordFields from '@/components/identity/PasswordFields.vue'
import { identityApi } from '@/api/identity'
import { passwordError } from '@/domain/identity'
import { useAuthStore } from '@/stores/auth'
import { useAsyncTask } from '@/composables/useAsyncTask'

const auth = useAuthStore()
const router = useRouter()
const oldPassword = ref(''),
  password = ref(''),
  confirmation = ref('')
const errors = reactive<Record<string, string>>({})
const task = useAsyncTask()
const minimum = auth.session?.password_min_length ?? 12
async function logout() {
  await task.run(async () => {
    await auth.logout()
    await router.replace({ name: 'login' })
  })
}
async function submit() {
  errors.old = oldPassword.value ? '' : '请输入当前临时密码'
  errors.password = passwordError(
    password.value,
    auth.user?.username ?? '',
    minimum,
    oldPassword.value,
  )
  errors.confirmation = confirmation.value === password.value ? '' : '两次输入的新密码不一致'
  if (Object.values(errors).some(Boolean)) return
  await task.run(async () => {
    await identityApi.completePassword(oldPassword.value, password.value)
    auth.clear('password-changed')
    await router.replace({ name: 'login', query: { reason: 'password-changed' } })
  })
  oldPassword.value = ''
}
onBeforeUnmount(() => {
  oldPassword.value = ''
  password.value = ''
  confirmation.value = ''
})
</script>
<template>
  <form class="flex flex-col gap-[18px]" @submit.prevent="submit">
    <p class="flex items-center gap-3 text-[14px] font-medium text-accent">
      <AppIcon name="key-round" :size="23" />设置个人密码
    </p>
    <h2 class="text-[30px] font-semibold">首次登录，设置新密码</h2>
    <p class="text-[15px] leading-relaxed text-muted">
      当前使用临时密码。完成改密后，请使用新密码重新登录。
    </p>
    <div class="flex items-center gap-2 rounded-[5px] bg-panel px-4 py-3">
      <AppIcon name="user-round" class="text-accent" /><span
        >{{ auth.user?.display_name }} / {{ auth.user?.username }}</span
      >
    </div>
    <PasswordFields
      v-model:old-password="oldPassword"
      v-model:new-password="password"
      v-model:confirmation="confirmation"
      :minimum="minimum"
      :errors="errors"
      first
    />
    <InfoBar v-if="task.error.value" :text="task.error.value" tone="danger" size="md" />
    <ActionBtn type="submit" variant="primary" class="h-12! w-full" :loading="task.busy.value"
      >保存密码并重新登录</ActionBtn
    >
    <p class="text-[12px] text-muted">当前仅允许设置密码，尚未进入业务工作台。</p>
    <div class="flex justify-between border-t border-line pt-4 text-[12px] text-muted">
      <span>修改成功后，原有登录状态将失效。</span
      ><button type="button" class="text-accent" :disabled="task.busy.value" @click="logout">
        退出
      </button>
    </div>
  </form>
</template>
