<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage } from 'element-plus'
import ActionBtn from '@/components/common/ActionBtn.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import FormField from '@/components/common/FormField.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import PageHeading from '@/components/common/PageHeading.vue'
import UserNavigation from '@/components/identity/UserNavigation.vue'
import SecurityDialog from '@/components/identity/SecurityDialog.vue'
import { useAsyncTask } from '@/composables/useAsyncTask'
import { useSecurityAction } from '@/composables/useSecurityAction'
import { usersApi } from '@/api/identity'
import type { LoginPolicy } from '@/types/identity'

const form = reactive<LoginPolicy>({
  failed_login_limit: 6,
  lock_minutes: 10,
  idle_timeout_minutes: 10,
  password_min_length: 12,
  login_notice: '',
})
const errors = reactive<Record<string, string>>({})
const ready = ref(false)
const task = useAsyncTask()
async function load() {
  await task.run(async () => {
    Object.assign(form, await usersApi.policy())
    ready.value = true
  })
}
const security = useSecurityAction(load)
function save() {
  if (!ready.value) return
  for (const [key, min, max] of [
    ['failed_login_limit', 3, 6],
    ['lock_minutes', 10, 30],
    ['idle_timeout_minutes', 10, 30],
    ['password_min_length', 12, 32],
  ] as const) {
    errors[key] =
      Number.isInteger(form[key]) && form[key] >= min && form[key] <= max
        ? ''
        : `请输入 ${min}–${max} 之间的整数`
  }
  errors.login_notice = Array.from(form.login_notice).length > 500 ? '通知不能超过 500 个字符' : ''
  if (Object.values(errors).some(Boolean)) return
  const payload = { ...form }
  security.request(
    {
      title: '保存账户与会话保护设置',
      message: '保存后立即生效。缩短超时时间可能使部分现有会话需要重新登录。',
      confirmLabel: '保存设置',
    },
    async () => {
      Object.assign(form, await usersApi.savePolicy(payload))
      ElMessage.success('账户与会话保护设置已保存')
    },
  )
}
const values = [
  {
    key: 'failed_login_limit',
    label: '连续登录失败阈值',
    hint: '范围 3–6 次，达到阈值锁定账号',
    suffix: '次',
  },
  {
    key: 'lock_minutes',
    label: '账号锁定时间',
    hint: '范围 10–30 分钟，到期可重新尝试',
    suffix: '分钟',
  },
] as const
onMounted(load)
</script>
<template>
  <div class="identity-page">
    <PageHeading
      title="账户与会话保护"
      description="配置登录保护与密码规则，设置保存后生效。"
      breadcrumb="用户与权限 / 登录保护"
      ><template #actions
        ><ActionBtn
          variant="primary"
          icon="check"
          :disabled="!ready || task.busy.value"
          @click="save"
          >保存设置</ActionBtn
        ></template
      ></PageHeading
    ><UserNavigation />
    <AsyncContent :loading="task.busy.value" :error="task.error.value" @retry="load">
      <div
        v-if="ready"
        class="grid min-h-0 flex-1 gap-6 xl:grid-cols-[minmax(0,1.95fr)_minmax(300px,1fr)]"
      >
        <form class="panel flex flex-col gap-6 overflow-y-auto p-7" @submit.prevent="save">
          <section class="identity-policy-section">
            <h2 class="text-[18px] font-semibold">账户保护</h2>
            <div class="grid gap-6 md:grid-cols-2">
              <FormField
                v-for="item in values"
                :key="item.key"
                :label="item.label"
                :hint="item.hint"
                :error="errors[item.key]"
                v-slot="field"
                ><el-input
                  :id="field.id"
                  v-model.number="form[item.key]"
                  inputmode="numeric"
                  :aria-invalid="field.invalid"
                  ><template #suffix>{{ item.suffix }}</template></el-input
                ></FormField
              >
            </div>
          </section>
          <section class="identity-policy-section">
            <h2 class="text-[18px] font-semibold">会话安全</h2>
            <FormField
              label="空闲超时时间"
              hint="范围 10–30 分钟；后台自动刷新不计为用户操作"
              :error="errors.idle_timeout_minutes"
              v-slot="field"
              ><el-input
                :id="field.id"
                v-model.number="form.idle_timeout_minutes"
                inputmode="numeric"
                ><template #suffix>分钟</template></el-input
              ></FormField
            >
          </section>
          <section class="identity-policy-section">
            <h2 class="text-[18px] font-semibold">密码规则</h2>
            <div class="grid gap-6 md:grid-cols-2">
              <FormField
                label="密码最小长度"
                hint="可设置 12–32，最大长度固定为 32"
                :error="errors.password_min_length"
                v-slot="field"
                ><el-input
                  :id="field.id"
                  v-model.number="form.password_min_length"
                  inputmode="numeric"
                  ><template #suffix>个字符</template></el-input
                ></FormField
              >
              <div class="flex flex-col gap-2">
                <p class="text-[14px] text-muted">字符要求</p>
                <p class="text-[14px]">大写、小写、数字、特殊符号至少三类</p>
                <p class="text-[12px] text-muted">首次及重置后登录必须修改密码</p>
              </div>
            </div>
          </section>
          <section class="identity-policy-section">
            <h2 class="text-[18px] font-semibold">登录页通知</h2>
            <FormField
              label="通知内容"
              hint="最多 500 个字符，仅显示纯文本。"
              :error="errors.login_notice"
              v-slot="field"
              ><el-input
                :id="field.id"
                v-model="form.login_notice"
                type="textarea"
                :rows="3"
                placeholder="输入显示在登录页的站内通知"
            /></FormField>
          </section>
        </form>
        <aside class="flex flex-col gap-5">
          <InfoBar
            title="保存后立即生效"
            text="缩短超时时间后，现有会话按新的空闲期限判断。"
            tone="warn"
            size="md"
          />
          <section class="panel flex flex-col gap-5 p-6">
            <h2 class="text-[18px] font-semibold">适用于站内全部账号</h2>
            <div
              v-for="item in [
                {
                  icon: 'shield-check',
                  title: '仅管理员可修改',
                  text: '保存前需要验证你的当前密码。',
                },
                {
                  icon: 'key-round',
                  title: '更新密码规则',
                  text: '用户下次设置密码时按新规则校验。',
                },
                {
                  icon: 'history',
                  title: '保护关键操作',
                  text: '关键变更需要确认并验证管理员身份。',
                },
              ]"
              :key="item.title"
              class="flex gap-3"
            >
              <AppIcon
                :name="item.icon === 'history' ? 'file-clock' : item.icon"
                :size="20"
                class="mt-0.5 text-accent"
              />
              <div>
                <h3 class="text-[14px] font-medium">{{ item.title }}</h3>
                <p class="mt-1 text-[13px] leading-relaxed text-muted">{{ item.text }}</p>
              </div>
            </div>
          </section>
          <p class="text-[13px] leading-relaxed text-muted">
            登录保护设置不改变用户角色及设备授权范围。
          </p>
        </aside>
      </div> </AsyncContent
    ><SecurityDialog
      v-bind="security.state"
      @confirm="security.confirm"
      @cancel="security.cancel"
    />
  </div>
</template>
