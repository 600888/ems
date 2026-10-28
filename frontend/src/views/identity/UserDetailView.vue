<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import PageHeading from '@/components/common/PageHeading.vue'
import UnderlineTabs from '@/components/common/UnderlineTabs.vue'
import RoleBadge from '@/components/identity/RoleBadge.vue'
import RolePicker from '@/components/identity/RolePicker.vue'
import ScopePicker from '@/components/identity/ScopePicker.vue'
import PermissionMark from '@/components/identity/PermissionMark.vue'
import SessionList from '@/components/identity/SessionList.vue'
import SecurityDialog from '@/components/identity/SecurityDialog.vue'
import { useSecurityAction } from '@/composables/useSecurityAction'
import { useAsyncTask } from '@/composables/useAsyncTask'
import { useAuthStore } from '@/stores/auth'
import { useUsersStore } from '@/stores/users'
import { usersApi } from '@/api/identity'
import { formatTime, permissionMatrix, roleLabel, scopeLabels } from '@/domain/identity'
import type { ManagedUser, UserAccess, SessionSummary } from '@/types/identity'

const route = useRoute(),
  router = useRouter(),
  auth = useAuthStore(),
  users = useUsersStore()
const user = ref<ManagedUser | null>(null)
const sessions = ref<SessionSummary[]>([])
const name = ref('')
const nameError = ref('')
const task = useAsyncTask(),
  sessionTask = useAsyncTask(),
  saveTask = useAsyncTask()
const access = reactive<UserAccess>({ role_code: 'observer', scope_mode: 'selected', scopes: [] })
const tabs = [
  { key: 'basic', label: '基本资料' },
  { key: 'access', label: '角色与范围' },
  { key: 'security', label: '安全与会话' },
]
const active = computed(() =>
  tabs.some((tab) => tab.key === route.params.tab) ? String(route.params.tab) : 'basic',
)
const own = computed(() => user.value?.user_id === auth.user?.user_id)
const locked = computed(() =>
  Boolean(user.value?.locked_until && Date.parse(user.value.locked_until) > Date.now()),
)
async function loadSessions() {
  if (user.value)
    await sessionTask.run(async () => {
      sessions.value = (await usersApi.sessions(user.value!.user_id)).items
    })
}
async function load() {
  await task.run(async () => {
    const id = String(route.params.userId)
    const [result] = await Promise.all([usersApi.detail(id), users.loadCapabilities()])
    if (id !== String(route.params.userId)) return
    user.value = result
    name.value = result.display_name
    Object.assign(access, {
      role_code: result.role_code,
      scope_mode: result.scope_mode,
      scopes: result.scopes.map((scope) => ({ ...scope })),
    })
    await loadSessions()
  })
}
const security = useSecurityAction(load)
watch(
  () => route.params.userId,
  () => {
    user.value = null
    void load()
  },
  { immediate: true },
)
watch(
  () => access.role_code,
  (role) => {
    if (role !== 'admin' && access.scope_mode === 'all') {
      access.scope_mode = 'selected'
      access.scopes = []
    }
  },
)
function saveAccess() {
  if (!user.value || own.value) return
  const current = user.value
  security.request(
    {
      title: '保存角色与设备授权',
      target: `${current.display_name} / ${current.username}`,
      message: '修改立即生效，该用户的全部会话将退出。请确认角色及设备范围。',
      confirmLabel: '保存授权',
    },
    async () => {
      await usersApi.access(current.user_id, access, current.edit_token)
      ElMessage.success('角色与设备范围已更新，用户需重新登录')
    },
  )
}
async function saveName() {
  if (!user.value || own.value) return
  nameError.value =
    name.value.trim() && Array.from(name.value.trim()).length <= 64 ? '' : '姓名应为 1–64 个字符'
  if (nameError.value) return
  await saveTask.run(async () => {
    user.value = await usersApi.update(
      user.value!.user_id,
      name.value.trim(),
      user.value!.edit_token,
    )
    ElMessage.success('用户资料已保存')
  })
}
function resetPassword() {
  if (!user.value || own.value) return
  const current = user.value
  security.request(
    {
      title: '重置临时密码',
      target: `${current.display_name} / ${current.username}`,
      message: '将撤销该账号全部会话。临时密码 24 小时内有效，用户下次登录必须设置个人密码。',
      temporaryPassword: true,
      username: current.username,
      minimum: users.capabilities?.password_min_length,
      confirmLabel: '确认重置',
    },
    async (temporary) => {
      await usersApi.action('reset-password', current.user_id, current.edit_token, {
        temporary_password: temporary,
      })
      ElMessage.success('临时密码已重置')
    },
  )
}
function unlock() {
  if (!user.value || own.value) return
  const current = user.value
  security.request(
    {
      title: '解除账号锁定',
      target: `${current.display_name} / ${current.username}`,
      message: '只清除失败计数和临时锁定，不改变账号启用状态。',
    },
    async () => {
      await usersApi.action('unlock', current.user_id, current.edit_token)
      ElMessage.success('账号锁定已解除')
    },
  )
}
function revoke(session?: SessionSummary) {
  if (!user.value) return
  const current = user.value
  security.request(
    {
      title: session ? '退出会话' : '退出全部会话',
      target: `${current.display_name} / ${current.username}`,
      message: session ? `将结束 ${session.client} 的访问。` : '该账号所有已登录终端将立即退出。',
      destructive: true,
      confirmLabel: '确认退出',
    },
    async () => {
      await usersApi.revokeSession(current.user_id, session?.session_id)
      ElMessage.success('会话已退出')
      await auth.restore(true)
    },
  )
}
</script>
<template>
  <div class="identity-page">
    <PageHeading
      :title="user ? `${user.display_name} · ${user.username}` : '用户详情'"
      :description="
        user
          ? `${user.status === 'enabled' ? '账号已启用' : '账号已停用'} · ${roleLabel(user.role_code)} · 最近登录 ${formatTime(user.last_login_at, true)}`
          : ''
      "
      :breadcrumb="`用户与权限 / ${user?.display_name ?? '用户详情'} / ${tabs.find((tab) => tab.key === active)?.label}`"
      ><template #actions
        ><ActionBtn icon="chevron-left" @click="router.push({ name: 'users' })"
          >返回用户列表</ActionBtn
        ><ActionBtn
          v-if="active === 'access' && auth.can('user.grant') && !own"
          variant="primary"
          icon="check"
          :disabled="task.busy.value || !user"
          @click="saveAccess"
          >保存授权</ActionBtn
        ></template
      ></PageHeading
    >
    <UnderlineTabs
      :items="tabs"
      :model-value="active"
      :height="48"
      class="px-0! [&_button]:text-[15px]!"
      @update:model-value="router.push(`/admin/users/${route.params.userId}/${$event}`)"
    />
    <AsyncContent :loading="task.busy.value" :error="task.error.value" @retry="load">
      <template v-if="user">
        <InfoBar
          v-if="own"
          title="当前登录账号"
          text="请通过个人中心修改姓名或密码。不能在这里修改自己的角色、状态和设备范围。"
          size="md"
        />
        <div
          v-if="active === 'access'"
          class="grid min-h-0 flex-1 gap-6 xl:grid-cols-[minmax(0,1.95fr)_minmax(300px,1fr)]"
        >
          <section class="panel flex min-h-0 flex-col gap-[22px] overflow-y-auto p-6">
            <h2 class="text-[17px] font-semibold">用户角色</h2>
            <RolePicker v-model="access.role_code" :disabled="own" /><ScopePicker
              v-model="access.scopes"
              v-model:scope-mode="access.scope_mode"
              :role="access.role_code"
              :tree="users.tree"
              :disabled="own"
            />
          </section>
          <aside class="flex flex-col gap-5">
            <section class="panel flex flex-col gap-[18px] p-6">
              <h2 class="text-[18px] font-semibold">
                {{ roleLabel(access.role_code) }} · 权限预览
              </h2>
              <div
                v-for="row in permissionMatrix.filter((row) =>
                  [
                    '实时监视、历史与曲线',
                    '查看通道与绑定详情',
                    '确认告警、填写处置备注',
                    '提交遥控 / 遥调意图',
                    '新增、修改、删除设备',
                    '分配角色与设备授权范围',
                    '全局审计查询、系统指标',
                  ].includes(row.label),
                )"
                :key="row.label"
                class="flex items-center gap-3"
              >
                <PermissionMark :allowed="row.roles.includes(access.role_code)" /><span
                  class="text-[14px]"
                  :class="!row.roles.includes(access.role_code) && 'text-muted'"
                  >{{ row.label }}</span
                >
              </div>
              <router-link :to="{ name: 'roles' }" class="identity-text-action"
                >查看三类角色权限矩阵 ↗</router-link
              >
            </section>
            <InfoBar
              title="保存后将使该用户退出"
              :text="`${user.display_name}需要重新登录。保存前请验证你自己的当前密码。`"
              tone="warn"
              size="md"
            />
            <p class="text-[13px] leading-relaxed text-muted">
              操作权限与设备范围同时生效；遥控、遥调仍受运行联锁和安全校验约束。
            </p>
          </aside>
        </div>
        <div
          v-else-if="active === 'security'"
          class="grid min-h-0 flex-1 gap-6 xl:grid-cols-[400px_minmax(0,1fr)]"
        >
          <aside class="panel flex flex-col gap-[22px] p-6">
            <div class="flex items-center gap-[14px]">
              <AppIcon name="circle-user" :size="44" class="text-accent" />
              <div>
                <h2 class="text-[22px] font-semibold">{{ user.display_name }}</h2>
                <p class="mt-1 text-muted">{{ user.username }}</p>
              </div>
            </div>
            <div class="flex gap-3">
              <RoleBadge :role="user.role_code" /><span
                :class="user.status === 'enabled' ? 'text-ok' : 'text-muted'"
                >{{ user.status === 'enabled' ? '已启用' : '已停用' }}</span
              >
            </div>
            <dl class="flex flex-col gap-5">
              <div>
                <dt class="text-[12px] text-muted">密码状态</dt>
                <dd class="mt-1.5 text-[15px]">
                  {{ user.must_change_password ? '待首次改密' : '已设置个人密码' }}
                </dd>
              </div>
              <div>
                <dt class="text-[12px] text-muted">最近修改密码</dt>
                <dd class="mt-1.5 text-[15px]">{{ formatTime(user.password_changed_at) }}</dd>
              </div>
              <div>
                <dt class="text-[12px] text-muted">失败锁定</dt>
                <dd class="mt-1.5 text-[15px]" :class="locked && 'text-warn'">
                  {{ locked ? `锁定至 ${formatTime(user.locked_until)}` : '未锁定' }}
                </dd>
              </div>
              <div>
                <dt class="text-[12px] text-muted">当前有效会话</dt>
                <dd class="mt-1.5 text-[15px]">{{ sessions.length }} / 3 个</dd>
              </div>
            </dl>
            <div class="flex-1" />
            <template v-if="!own && auth.can('user.security')"
              ><ActionBtn v-if="locked" icon="lock-keyhole" @click="unlock">解除锁定</ActionBtn
              ><ActionBtn icon="key-round" @click="resetPassword">重置临时密码</ActionBtn
              ><InfoBar
                title="重置后需重新登录"
                text="将撤销全部会话，并要求用户首次登录设置个人密码。"
                tone="warn"
                size="md"
            /></template>
          </aside>
          <SessionList
            :sessions="sessions"
            :loading="sessionTask.busy.value"
            :error="sessionTask.error.value"
            @retry="loadSessions"
            @revoke="revoke"
            @revoke-all="revoke()"
          />
        </div>
        <form
          v-else
          class="panel flex max-w-[1000px] flex-col gap-6 p-7"
          @submit.prevent="saveName"
        >
          <h2 class="text-[18px] font-semibold">基本资料</h2>
          <div class="grid gap-6 sm:grid-cols-2">
            <FormField label="用户名" hint="用户名创建后不可修改" v-slot="field"
              ><el-input :id="field.id" :model-value="user.username" disabled /></FormField
            ><FormField label="姓名" :error="nameError" v-slot="field"
              ><el-input :id="field.id" v-model="name" :disabled="own || saveTask.busy.value"
            /></FormField>
          </div>
          <div class="flex items-center gap-4">
            <RoleBadge :role="user.role_code" /><span class="text-[13px] text-muted">{{
              user.scope_mode === 'all'
                ? '全部站与设备'
                : scopeLabels(user.scopes, users.tree).join('；') || '未分配设备范围'
            }}</span>
          </div>
          <InfoBar
            v-if="saveTask.error.value"
            :text="saveTask.error.value"
            tone="danger"
            size="md"
          />
          <div class="flex justify-end">
            <ActionBtn v-if="own" @click="router.push('/profile')">进入个人中心</ActionBtn
            ><ActionBtn v-else type="submit" variant="primary" :loading="saveTask.busy.value"
              >保存资料</ActionBtn
            >
          </div>
        </form>
      </template>
    </AsyncContent>
    <SecurityDialog v-bind="security.state" @confirm="security.confirm" @cancel="security.cancel" />
  </div>
</template>
