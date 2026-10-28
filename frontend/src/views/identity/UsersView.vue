<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import PageHeading from '@/components/common/PageHeading.vue'
import StatusTag from '@/components/common/StatusTag.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import RoleBadge from '@/components/identity/RoleBadge.vue'
import UserNavigation from '@/components/identity/UserNavigation.vue'
import UserCreateDrawer from '@/components/identity/UserCreateDrawer.vue'
import SecurityDialog from '@/components/identity/SecurityDialog.vue'
import { useSecurityAction } from '@/composables/useSecurityAction'
import { useAuthStore } from '@/stores/auth'
import { useUsersStore } from '@/stores/users'
import { formatTime, roles, scopeLabels } from '@/domain/identity'
import { usersApi } from '@/api/identity'
import { ApiError } from '@/api/http'
import type { ManagedUser } from '@/types/identity'

const auth = useAuthStore(),
  users = useUsersStore(),
  router = useRouter()
const drawer = ref(false)
const security = useSecurityAction(users.load)
const statItems = [
  { key: 'total', label: '全部账号', color: 'text-ink' },
  { key: 'enabled', label: '已启用', color: 'text-ok' },
  { key: 'disabled', label: '已停用', color: 'text-muted' },
  { key: 'locked', label: '临时锁定', color: 'text-warn' },
] as const
const locked = (user: ManagedUser) =>
  Boolean(user.locked_until && Date.parse(user.locked_until) > Date.now())
function search() {
  users.query.page = 1
  void users.load()
}
function resolveCreated(username: string) {
  users.query.keyword = username
  search()
}
function manage(user: ManagedUser) {
  void router.push(
    user.user_id === auth.user?.user_id ? '/profile' : `/admin/users/${user.user_id}/access`,
  )
}
async function action(user: ManagedUser, command: string) {
  const target = `${user.display_name} / ${user.username}`
  const descriptions: Record<string, string> = {
    disable: '停用后，该账号将无法登录，全部在线终端会立即退出。设备采集服务不受影响。',
    enable: '启用后，用户可使用个人密码或未过期的临时密码登录。',
    delete: '删除后将撤销全部会话，用户名不再允许复用，历史审计记录仍然保留。',
    unlock: '仅清除失败计数与临时锁定，不会自动启用停用账号。',
    reset: '将退出该账号全部会话。临时密码 24 小时内有效，下次登录必须修改。',
  }
  const titles: Record<string, string> = {
    disable: '停用账号',
    enable: '启用账号',
    delete: '删除用户',
    unlock: '解除账号锁定',
    reset: '重置临时密码',
  }
  security.request(
    {
      title: titles[command]!,
      target,
      message: descriptions[command]!,
      destructive: ['disable', 'delete'].includes(command),
      confirmLabel: command === 'reset' ? '确认重置' : titles[command],
      temporaryPassword: command === 'reset',
      username: user.username,
      minimum: users.capabilities?.password_min_length,
    },
    async (temporary) => {
      const current = await usersApi.detail(user.user_id)
      if (current.edit_token !== user.edit_token)
        throw new ApiError(409, 'edit_conflict', '该用户信息已变化，请关闭并刷新列表后重新确认')
      await usersApi.action(
        command === 'reset'
          ? 'reset-password'
          : command === 'delete'
            ? 'delete'
            : command === 'unlock'
              ? 'unlock'
              : 'set-status',
        user.user_id,
        user.edit_token,
        command === 'reset'
          ? { temporary_password: temporary }
          : ['enable', 'disable'].includes(command)
            ? { status: command === 'enable' ? 'enabled' : 'disabled' }
            : {},
      )
      ElMessage.success('操作成功，账号状态已更新')
    },
  )
}
onMounted(() => {
  void users.load()
  void users.loadCapabilities().catch(() => undefined)
})
</script>
<template>
  <div class="identity-page">
    <PageHeading
      title="用户管理"
      description="管理站内账号、角色与设备访问范围。"
      breadcrumb="系统管理 / 用户与权限"
      ><template #actions
        ><ActionBtn
          v-if="auth.can('user.write') && auth.can('user.grant')"
          variant="primary"
          icon="plus"
          @click="drawer = true"
          >新建用户</ActionBtn
        ></template
      ></PageHeading
    >
    <UserNavigation />
    <div class="flex flex-wrap items-center gap-x-8 gap-y-3 py-1">
      <div v-for="item in statItems" :key="item.key" class="flex items-center gap-[10px]">
        <span class="text-[26px] font-semibold tabular-nums" :class="item.color">{{
          users.stats[item.key]
        }}</span
        ><span class="text-[13px] text-muted">{{ item.label }}</span>
      </div>
    </div>
    <section class="panel flex min-h-[560px] shrink-0 flex-1 flex-col overflow-hidden lg:min-h-0">
      <form class="flex shrink-0 flex-wrap items-center gap-3 p-5" @submit.prevent="search">
        <el-input
          v-model="users.query.keyword"
          class="w-[330px]! max-w-full"
          placeholder="搜索用户名 / 姓名"
          clearable
          aria-label="搜索用户名或姓名"
          ><template #prefix><AppIcon name="search" /></template></el-input
        ><el-select
          v-model="users.query.role_code"
          class="w-[180px]!"
          aria-label="角色筛选"
          placeholder="全部角色"
          ><el-option label="全部角色" value="" /><el-option
            v-for="role in roles"
            :key="role.code"
            :label="role.label"
            :value="role.code" /></el-select
        ><el-select
          v-model="users.query.status"
          class="w-[180px]!"
          aria-label="状态筛选"
          placeholder="全部状态"
          ><el-option label="全部状态" value="" /><el-option
            label="已启用"
            value="enabled" /><el-option label="已停用" value="disabled" /></el-select
        ><ActionBtn type="submit" variant="primary" :disabled="users.busy">查询</ActionBtn
        ><ActionBtn @click="users.reset">重置</ActionBtn
        ><ActionBtn
          class="ml-auto"
          icon="refresh-cw"
          :loading="users.busy"
          aria-label="刷新用户列表"
          @click="users.load"
          >刷新</ActionBtn
        >
      </form>
      <AsyncContent
        :loading="users.busy"
        :error="users.error"
        :empty="users.total === 0"
        empty-title="没有匹配的用户"
        empty-description="尝试调整用户名、角色或状态筛选条件。"
        @retry="users.load"
        ><template #empty-action><ActionBtn @click="users.reset">清空筛选</ActionBtn></template>
        <div class="min-h-0 flex-1 overflow-auto">
          <table class="identity-table">
            <thead>
              <tr>
                <th scope="col">账号 / 姓名</th>
                <th scope="col">角色</th>
                <th scope="col">账号状态</th>
                <th scope="col">安全状态</th>
                <th scope="col">授权范围</th>
                <th scope="col">最近登录</th>
                <th scope="col">操作</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="user in users.rows" :key="user.user_id">
                <td>
                  <span class="block font-medium">{{ user.username }}</span
                  ><span class="mt-1 block text-[12px] text-muted"
                    >{{ user.display_name
                    }}{{ user.user_id === auth.user?.user_id ? ' · 当前账号' : '' }}</span
                  >
                </td>
                <td><RoleBadge :role="user.role_code" /></td>
                <td>
                  <StatusTag
                    :tone="user.status === 'enabled' ? 'ok' : 'mute'"
                    :label="user.status === 'enabled' ? '已启用' : '已停用'"
                  />
                </td>
                <td>
                  <span
                    v-if="locked(user)"
                    class="rounded bg-warn-soft px-2 py-1 text-[12px] text-warn"
                    :title="formatTime(user.locked_until)"
                    >临时锁定</span
                  ><span v-else>{{ user.must_change_password ? '待首次改密' : '正常' }}</span>
                </td>
                <td class="max-w-[310px]">
                  <span
                    class="block truncate"
                    :title="scopeLabels(user.scopes, users.tree).join('；')"
                    >{{
                      user.scope_mode === 'all'
                        ? '全部站与设备'
                        : scopeLabels(user.scopes, users.tree).join('；') || '未分配设备范围'
                    }}</span
                  >
                </td>
                <td class="whitespace-nowrap">{{ formatTime(user.last_login_at, true) }}</td>
                <td>
                  <div class="flex items-center gap-3">
                    <button type="button" class="identity-text-action" @click="manage(user)">
                      {{ user.user_id === auth.user?.user_id ? '个人中心' : '管理' }}</button
                    ><el-dropdown
                      v-if="user.user_id !== auth.user?.user_id && auth.can('user.write')"
                      trigger="click"
                      @command="action(user, String($event))"
                      ><button
                        type="button"
                        class="px-2 text-muted"
                        :aria-label="`${user.display_name}的更多操作`"
                      >
                        •••</button
                      ><template #dropdown
                        ><el-dropdown-menu
                          ><el-dropdown-item
                            :command="user.status === 'enabled' ? 'disable' : 'enable'"
                            >{{
                              user.status === 'enabled' ? '停用账号' : '启用账号'
                            }}</el-dropdown-item
                          ><el-dropdown-item v-if="auth.can('user.security')" command="reset"
                            >重置临时密码</el-dropdown-item
                          ><el-dropdown-item
                            v-if="locked(user) && auth.can('user.security')"
                            command="unlock"
                            >解除锁定</el-dropdown-item
                          ><el-dropdown-item divided command="delete"
                            ><span class="text-danger">删除用户</span></el-dropdown-item
                          ></el-dropdown-menu
                        ></template
                      ></el-dropdown
                    >
                  </div>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </AsyncContent>
      <footer
        class="flex shrink-0 flex-wrap items-center justify-between gap-3 border-t border-line px-5 py-4"
      >
        <span class="text-[13px] text-muted">共 {{ users.total }} 个账号</span
        ><el-pagination
          v-model:current-page="users.query.page"
          v-model:page-size="users.query.page_size"
          :total="users.total"
          :page-sizes="[10, 20, 50, 100]"
          layout="sizes, prev, pager, next"
          @current-change="users.load"
          @size-change="search"
        />
      </footer>
    </section>
    <p class="shrink-0 text-[12px] text-muted">
      停用、重置密码或调整授权后，该用户的现有会话将失效。
    </p>
    <UserCreateDrawer v-model="drawer" @created="users.load" @resolve="resolveCreated" />
    <SecurityDialog v-bind="security.state" @confirm="security.confirm" @cancel="security.cancel" />
  </div>
</template>
