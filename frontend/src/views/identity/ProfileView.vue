<script setup lang="ts">
import { onBeforeUnmount, onMounted, reactive, ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import AppIcon from '@/components/common/AppIcon.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import PageHeading from '@/components/common/PageHeading.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import RoleBadge from '@/components/identity/RoleBadge.vue'
import PasswordFields from '@/components/identity/PasswordFields.vue'
import SessionList from '@/components/identity/SessionList.vue'
import SecurityDialog from '@/components/identity/SecurityDialog.vue'
import { useSecurityAction } from '@/composables/useSecurityAction'
import { useAsyncTask } from '@/composables/useAsyncTask'
import { useAuthStore } from '@/stores/auth'
import { identityApi } from '@/api/identity'
import { formatTime, passwordError } from '@/domain/identity'
import type { ManagedUser, SessionSummary } from '@/types/identity'

const auth = useAuthStore(),
  router = useRouter()
const profile = ref<ManagedUser | null>(null),
  sessions = ref<SessionSummary[]>([])
const name = ref(''),
  nameError = ref(''),
  oldPassword = ref(''),
  password = ref(''),
  confirmation = ref('')
const minimum = ref(12)
const errors = reactive<Record<string, string>>({})
const task = useAsyncTask(),
  nameTask = useAsyncTask(),
  passwordTask = useAsyncTask(),
  sessionTask = useAsyncTask()
async function loadSessions() {
  await sessionTask.run(async () => {
    sessions.value = (await identityApi.sessions()).items
  })
}
async function load() {
  await task.run(async () => {
    const [user, options] = await Promise.all([identityApi.profile(), identityApi.options()])
    profile.value = user
    name.value = user.display_name
    minimum.value = options.password_min_length
    await loadSessions()
  })
}
const security = useSecurityAction(loadSessions)
async function saveProfile() {
  nameError.value =
    name.value.trim() && Array.from(name.value.trim()).length <= 64 ? '' : '姓名应为 1–64 个字符'
  if (nameError.value) return
  await nameTask.run(async () => {
    profile.value = await identityApi.updateProfile(name.value.trim())
    await auth.restore(true)
    ElMessage.success('个人资料已保存')
  })
}
async function changePassword() {
  errors.old = oldPassword.value ? '' : '请输入当前密码'
  errors.password = passwordError(
    password.value,
    profile.value?.username ?? '',
    minimum.value,
    oldPassword.value,
  )
  errors.confirmation = confirmation.value === password.value ? '' : '两次输入的新密码不一致'
  if (Object.values(errors).some(Boolean)) return
  await passwordTask.run(async () => {
    await identityApi.changePassword(oldPassword.value, password.value)
    auth.clear('password-changed')
    await router.replace({ name: 'login', query: { reason: 'password-changed' } })
  })
  oldPassword.value = ''
}
function revoke(session?: SessionSummary) {
  security.request(
    {
      title: session ? '退出登录设备' : '退出全部登录设备',
      target: session?.client,
      message: session
        ? '此设备将立即停止访问，需要重新登录。'
        : '包括当前设备在内的全部终端将退出，请重新登录。',
      password: false,
      destructive: true,
      confirmLabel: '确认退出',
    },
    async () => {
      if (session) await identityApi.revokeSession(session.session_id)
      else await auth.logout(true)
      ElMessage.success('会话已退出')
      if (session?.current) auth.clear('logout')
    },
  )
}
onMounted(load)
onBeforeUnmount(() => {
  oldPassword.value = ''
  password.value = ''
  confirmation.value = ''
})
</script>
<template>
  <div class="identity-page px-0 py-2 lg:px-8 xl:px-14">
    <PageHeading title="个人中心" description="管理自己的资料、密码与登录设备。"
      ><template #actions
        ><ActionBtn
          icon="chevron-left"
          @click="router.push(auth.landing === '/profile' ? '/profile' : auth.landing)"
          >返回工作台</ActionBtn
        ></template
      ></PageHeading
    >
    <AsyncContent :loading="task.busy.value" :error="task.error.value" @retry="load"
      ><div v-if="profile" class="grid min-h-0 flex-1 gap-7 xl:grid-cols-[390px_minmax(0,1fr)]">
        <aside class="panel flex flex-col gap-6 p-7">
          <AppIcon name="circle-user" :size="64" class="text-accent" />
          <h2 class="text-[26px] font-semibold">{{ profile.display_name }}</h2>
          <p class="text-[15px] text-muted">{{ profile.username }}</p>
          <RoleBadge :role="profile.role_code" />
          <p class="text-[13px] text-muted">角色和设备范围由管理员分配。</p>
          <div class="border-t border-line pt-6">
            <h3 class="mb-4 text-[15px] font-medium">当前设备范围</h3>
            <p v-if="profile.scope_mode === 'all'">全部站与设备</p>
            <p v-else-if="!profile.scopes.length" class="text-muted">
              未分配设备范围，仅可使用个人中心。
            </p>
            <ul v-else class="flex flex-col gap-3 text-[13px] text-muted">
              <li
                v-for="(scope, index) in profile.scopes"
                :key="`${scope.station_id}/${scope.device_root_id}`"
                class="flex items-center gap-2"
              >
                <AppIcon name="list-tree" class="text-accent" />{{
                  profile.scope_labels?.[index] ??
                  (scope.device_root_id ? `${scope.device_root_id} 及全部下级` : '已授权全站')
                }}
              </li>
            </ul>
          </div>
          <div class="flex-1" />
          <div>
            <p class="text-[12px] text-muted">本次登录</p>
            <p class="mt-2 text-[14px]">{{ formatTime(profile.last_login_at) }}</p>
          </div>
          <p class="text-[12px] leading-relaxed text-muted">发现异常登录请及时改密并联系管理员。</p>
        </aside>
        <div class="flex min-h-0 flex-col gap-[22px]">
          <form class="panel flex flex-col gap-5 p-7" @submit.prevent="saveProfile">
            <h2 class="text-[18px] font-semibold">基本资料</h2>
            <div class="grid gap-6 sm:grid-cols-2">
              <FormField label="用户名" hint="用户名创建后不可修改" v-slot="field"
                ><el-input :id="field.id" :model-value="profile.username" disabled /></FormField
              ><FormField label="姓名" :error="nameError" v-slot="field"
                ><el-input :id="field.id" v-model="name" :disabled="nameTask.busy.value"
              /></FormField>
            </div>
            <InfoBar
              v-if="nameTask.error.value"
              :text="nameTask.error.value"
              tone="danger"
              size="md"
            />
            <div class="flex justify-end">
              <ActionBtn type="submit" variant="primary" :loading="nameTask.busy.value"
                >保存资料</ActionBtn
              >
            </div>
          </form>
          <form class="panel flex flex-col gap-5 p-7" @submit.prevent="changePassword">
            <h2 class="text-[18px] font-semibold">修改密码</h2>
            <PasswordFields
              v-model:old-password="oldPassword"
              v-model:new-password="password"
              v-model:confirmation="confirmation"
              :minimum="minimum"
              :errors="errors"
              horizontal
            /><InfoBar
              v-if="passwordTask.error.value"
              :text="passwordTask.error.value"
              tone="danger"
              size="md"
            />
            <div class="flex flex-wrap items-center justify-between gap-3">
              <p class="text-[13px] text-warn">修改成功后，全部终端需要重新登录。</p>
              <ActionBtn
                type="submit"
                variant="primary"
                icon="key-round"
                :loading="passwordTask.busy.value"
                >修改密码</ActionBtn
              >
            </div>
          </form>
          <SessionList
            :sessions="sessions"
            :loading="sessionTask.busy.value"
            :error="sessionTask.error.value"
            compact
            @retry="loadSessions"
            @revoke="revoke"
            @revoke-all="revoke()"
          />
        </div></div></AsyncContent
    ><SecurityDialog
      v-bind="security.state"
      @confirm="security.confirm"
      @cancel="security.cancel"
    />
  </div>
</template>
