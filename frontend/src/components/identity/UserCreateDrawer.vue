<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import ScopePicker from './ScopePicker.vue'
import SecurityDialog from './SecurityDialog.vue'
import { useSecurityAction } from '@/composables/useSecurityAction'
import { useUsersStore } from '@/stores/users'
import { usersApi } from '@/api/identity'
import { ApiError, errorMessage } from '@/api/http'
import { passwordError, roles, scopeLabels } from '@/domain/identity'
import type { CreateUser } from '@/types/identity'

const props = defineProps<{ modelValue: boolean }>()
const emit = defineEmits<{
  'update:modelValue': [value: boolean]
  created: []
  resolve: [username: string]
}>()
const users = useUsersStore()
const form = reactive<CreateUser>({
  username: '',
  display_name: '',
  role_code: 'observer',
  status: 'disabled',
  scope_mode: 'selected',
  scopes: [],
  temporary_password: '',
})
const errors = reactive<Record<string, string>>({})
const error = ref(''),
  busy = ref(false),
  unknown = ref(false),
  expanded = ref(false)
const security = useSecurityAction()
const loading = computed(() => busy.value || security.state.busy)
const minimum = computed(() => users.capabilities?.password_min_length ?? 12)
watch(
  () => props.modelValue,
  async (open) => {
    if (!open) {
      form.temporary_password = ''
      return
    }
    Object.assign(form, {
      username: '',
      display_name: '',
      role_code: 'observer',
      status: 'disabled',
      scope_mode: 'selected',
      scopes: [],
      temporary_password: '',
    })
    for (const key of Object.keys(errors)) delete errors[key]
    error.value = ''
    unknown.value = false
    expanded.value = false
    await users.loadCapabilities().catch(() => undefined)
  },
)
watch(
  () => form.role_code,
  (role) => {
    if (role !== 'admin' && form.scope_mode === 'all') {
      form.scope_mode = 'selected'
      form.scopes = []
    }
  },
)
function close() {
  if (!loading.value) emit('update:modelValue', false)
}
function resolveCreated() {
  emit('resolve', form.username.trim().toLowerCase())
  close()
}
async function create(payload: CreateUser) {
  busy.value = true
  error.value = ''
  try {
    await usersApi.create(payload)
    ElMessage.success('用户已创建，首次登录需修改临时密码')
    emit('created')
    emit('update:modelValue', false)
  } catch (cause) {
    unknown.value = cause instanceof ApiError && cause.effect === 'Unknown'
    error.value =
      errorMessage(cause) + (unknown.value ? '。结果未确认，请先按用户名查询，勿重复创建。' : '')
    throw cause
  } finally {
    busy.value = false
    form.temporary_password = ''
  }
}
async function submit() {
  if (loading.value || unknown.value || !users.capabilities) return
  const username = form.username.trim().toLowerCase()
  errors.username = /^[a-z][a-z0-9_.-]{3,63}$/.test(username)
    ? ''
    : '4–64 位，以字母开头，可使用数字、点、下划线、连字符'
  errors.name =
    form.display_name.trim() && Array.from(form.display_name.trim()).length <= 64
      ? ''
      : '姓名应为 1–64 个字符'
  errors.password = passwordError(form.temporary_password, username, minimum.value)
  if (Object.values(errors).some(Boolean)) return
  const payload: CreateUser = {
    ...form,
    username,
    display_name: form.display_name.trim(),
    scopes: form.scopes.map((s) => ({ ...s })),
  }
  if (payload.role_code === 'admin' || payload.status === 'enabled') {
    security.request(
      {
        title: '确认创建并授予访问权限',
        target: `${payload.display_name} / ${username}`,
        message: '请确认账号角色与设备范围。启用后，用户可使用临时密码登录。',
        confirmLabel: '创建用户',
      },
      () => create(payload),
    )
  } else {
    await create(payload).catch(() => undefined)
  }
}
</script>
<template>
  <el-drawer
    :model-value="modelValue"
    :with-header="false"
    size="min(640px, 100vw)"
    class="ems-drawer identity-drawer"
    :close-on-click-modal="false"
    :close-on-press-escape="!loading"
    append-to-body
    :before-close="
      (done: () => void) => {
        if (!loading) {
          close()
          done()
        }
      }
    "
  >
    <form class="flex h-full flex-col" @submit.prevent="submit">
      <header class="flex shrink-0 items-start justify-between gap-4 px-8 py-6">
        <div>
          <h2 class="text-[24px] font-semibold">新建用户</h2>
          <p class="mt-1.5 text-[14px] text-muted">创建站内账号，并设置初始访问权限。</p>
        </div>
        <button type="button" aria-label="关闭新建用户" :disabled="loading" @click="close">
          <AppIcon name="x" :size="22" />
        </button>
      </header>
      <div class="flex min-h-0 flex-1 flex-col gap-5 overflow-y-auto px-8 pt-3 pb-6">
        <InfoBar
          v-if="users.capabilityError"
          tone="danger"
          :text="users.capabilityError"
          size="md"
        /><ActionBtn
          v-if="users.capabilityError"
          @click="users.loadCapabilities(true).catch(() => undefined)"
          >重新读取权限选项</ActionBtn
        >
        <div class="grid gap-5 sm:grid-cols-2">
          <FormField
            label="用户名"
            hint="创建后不可修改"
            :error="errors.username"
            required
            v-slot="field"
            ><el-input
              :id="field.id"
              v-model="form.username"
              autocomplete="off"
              placeholder="例如：chenxiao"
              :disabled="loading" /></FormField
          ><FormField label="姓名" :error="errors.name" required v-slot="field"
            ><el-input
              :id="field.id"
              v-model="form.display_name"
              placeholder="输入姓名"
              :disabled="loading"
          /></FormField>
        </div>
        <FormField
          label="用户角色"
          required
          :hint="roles.find((role) => role.code === form.role_code)?.description"
          v-slot="field"
          ><el-select :id="field.id" v-model="form.role_code" :disabled="loading"
            ><el-option
              v-for="role in roles"
              :key="role.code"
              :label="role.label"
              :value="role.code" /></el-select
        ></FormField>
        <router-link class="self-end text-[13px] text-accent" :to="{ name: 'roles' }" @click="close"
          >查看三类角色权限 ↗</router-link
        >
        <div class="flex items-center justify-between gap-3 border-y border-line py-4">
          <div>
            <h3 class="text-[14px]">账号状态</h3>
            <p class="mt-1 text-[12px] text-muted">初始为停用，分配范围后可手动启用。</p>
          </div>
          <div class="flex items-center gap-3">
            <span class="text-[13px] text-muted">{{
              form.status === 'enabled' ? '已启用' : '已停用'
            }}</span
            ><el-switch
              v-model="form.status"
              active-value="enabled"
              inactive-value="disabled"
              aria-label="启用账号"
              :disabled="loading"
            />
          </div>
        </div>
        <div
          v-if="!expanded"
          class="flex items-center gap-3 rounded-[5px] border border-line bg-surface p-4"
        >
          <AppIcon name="list-tree" :size="20" class="text-subtle" />
          <div class="min-w-0 flex-1">
            <h3 class="text-[14px]">
              {{
                form.scope_mode === 'all'
                  ? '全部站与设备'
                  : form.scopes.length
                    ? `已选择 ${form.scopes.length} 个范围`
                    : '尚未选择站或设备'
              }}
            </h3>
            <p class="mt-1 truncate text-[12px] text-muted">
              {{
                form.scopes.length
                  ? scopeLabels(form.scopes, users.tree).join('；')
                  : '空范围账号无法查看设备运行数据。'
              }}
            </p>
          </div>
          <button
            type="button"
            class="text-[13px] text-accent"
            :disabled="loading"
            @click="expanded = true"
          >
            选择范围
          </button>
        </div>
        <ScopePicker
          v-else
          v-model="form.scopes"
          v-model:scope-mode="form.scope_mode"
          :role="form.role_code"
          :tree="users.tree"
          :disabled="loading"
        />
        <FormField
          label="临时密码"
          :hint="`${minimum}–32 个字符，至少三类字符；首次登录必须修改。`"
          :error="errors.password"
          required
          v-slot="field"
          ><el-input
            :id="field.id"
            v-model="form.temporary_password"
            type="password"
            show-password
            autocomplete="new-password"
            placeholder="设置临时密码"
            :disabled="loading || unknown"
        /></FormField>
        <InfoBar
          title="首次登录必须修改密码"
          text="临时密码有效期 24 小时，请通过站内受控渠道告知用户。"
          size="md"
        />
        <InfoBar v-if="error" :text="error" tone="danger" size="md" /><ActionBtn
          v-if="unknown"
          @click="resolveCreated"
          >按用户名查询创建结果</ActionBtn
        >
      </div>
      <footer class="flex shrink-0 justify-end gap-3 border-t border-line px-8 py-5">
        <ActionBtn :disabled="loading" @click="close">取消</ActionBtn
        ><ActionBtn
          type="submit"
          variant="primary"
          :loading="loading"
          :disabled="!users.capabilities || unknown"
          >创建用户</ActionBtn
        >
      </footer>
    </form>
  </el-drawer>
  <SecurityDialog v-bind="security.state" @confirm="security.confirm" @cancel="security.cancel" />
</template>
