<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import AppIcon from '@/components/common/AppIcon.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { identityApi } from '@/api/identity'
import { ApiError, errorMessage } from '@/api/http'
import { safeReturnTo } from '@/domain/identity'
import { useAuthStore } from '@/stores/auth'

const auth = useAuthStore()
const route = useRoute()
const router = useRouter()
const username = ref('')
const password = ref('')
const busy = ref(false)
const ready = ref(false)
const error = ref('')
const notice = ref('')
const retryIn = ref(0)
let timer = 0
const reasonMessages: Record<string, string> = {
  expired: '会话已超时，请重新登录。',
  'password-changed': '密码已修改，请使用新密码重新登录。',
  unavailable: '身份服务暂时不可用，请稍后重试。',
}
async function load() {
  error.value = ''
  ready.value = false
  try {
    const [options] = await Promise.all([
      identityApi.options(),
      !auth.initialized || route.query.reason === 'unavailable' ? auth.restore(true) : undefined,
    ])
    notice.value = options.login_notice
    if (auth.session) {
      await router.replace(
        auth.session.kind === 'password_change'
          ? { name: 'first-password' }
          : safeReturnTo(route.query.return_to) || auth.landing,
      )
      return
    }
    if (route.query.reason === 'unavailable') {
      const query = { ...route.query }
      delete query.reason
      await router.replace({ name: 'login', query })
    }
    ready.value = true
  } catch (cause) {
    error.value = errorMessage(cause)
  }
}
async function submit() {
  if (busy.value || !ready.value || retryIn.value) return
  if (!username.value.trim() || !password.value) {
    error.value = '请输入用户名和密码'
    return
  }
  busy.value = true
  error.value = ''
  try {
    await auth.login(username.value.trim().toLowerCase(), password.value)
    await router.replace(
      auth.session?.kind === 'password_change'
        ? { name: 'first-password' }
        : safeReturnTo(route.query.return_to) || auth.landing,
    )
  } catch (cause) {
    error.value = errorMessage(cause)
    if (cause instanceof ApiError && cause.status === 429) {
      retryIn.value = Math.max(1, cause.retryAfter || 60)
      window.clearInterval(timer)
      timer = window.setInterval(() => {
        if (--retryIn.value <= 0) window.clearInterval(timer)
      }, 1000)
    }
  } finally {
    busy.value = false
    password.value = ''
  }
}
onMounted(load)
onBeforeUnmount(() => {
  window.clearInterval(timer)
  password.value = ''
})
</script>
<template>
  <form class="flex flex-col gap-6" @submit.prevent="submit">
    <p class="flex items-center gap-3 text-[14px] font-medium text-accent">
      <AppIcon name="shield-check" :size="23" />用户名与密码登录
    </p>
    <h2 class="text-[32px] font-semibold">欢迎登录</h2>
    <p class="text-[15px] text-muted">请输入管理员为你分配的站内账号。</p>
    <InfoBar
      v-if="reasonMessages[String(route.query.reason)]"
      :tone="route.query.reason === 'password-changed' ? 'ok' : 'warn'"
      :text="reasonMessages[String(route.query.reason)]!"
      size="md"
    />
    <FormField label="用户名" v-slot="field"
      ><el-input
        :id="field.id"
        v-model="username"
        class="identity-login-input"
        autocomplete="username"
        autofocus
        placeholder="请输入用户名"
        :disabled="busy"
        ><template #prefix><AppIcon name="user-round" :size="18" /></template></el-input
    ></FormField>
    <FormField label="密码" v-slot="field"
      ><el-input
        :id="field.id"
        v-model="password"
        class="identity-login-input"
        type="password"
        show-password
        autocomplete="current-password"
        placeholder="请输入密码"
        :disabled="busy"
        ><template #prefix><AppIcon name="lock-keyhole" :size="18" /></template></el-input
    ></FormField>
    <InfoBar v-if="error" tone="danger" :text="error" size="md" />
    <ActionBtn
      type="submit"
      variant="primary"
      class="h-[50px]! w-full text-[15px]!"
      :disabled="!ready || retryIn > 0"
      :loading="busy"
      >{{ busy ? '正在登录' : retryIn ? `${retryIn} 秒后重试` : '登录' }}</ActionBtn
    >
    <ActionBtn v-if="!ready && error" @click="load">重新连接服务</ActionBtn>
    <button
      type="button"
      class="self-center text-[13px] text-muted hover:text-accent"
      @click="ElMessage.info('请联系站内系统管理员重置临时密码，重置后首次登录需设置个人密码。')"
    >
      忘记密码？请联系系统管理员
    </button>
    <InfoBar v-if="notice" :text="notice" title="站内通知" size="md" />
    <p class="flex items-center gap-2 border-t border-line pt-5 text-[12px] text-muted">
      <AppIcon name="lock-keyhole" :size="14" />请在可信终端登录，使用完毕及时退出。
    </p>
  </form>
</template>
