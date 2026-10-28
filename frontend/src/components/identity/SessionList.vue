<script setup lang="ts">
import ActionBtn from '@/components/common/ActionBtn.vue'
import AppIcon from '@/components/common/AppIcon.vue'
import EmptyState from '@/components/common/EmptyState.vue'
import AsyncContent from '@/components/common/AsyncContent.vue'
import { formatTime } from '@/domain/identity'
import type { SessionSummary } from '@/types/identity'
withDefaults(
  defineProps<{
    sessions: SessionSummary[]
    loading?: boolean
    error?: string
    compact?: boolean
    canRevokeAll?: boolean
  }>(),
  { canRevokeAll: true },
)
const emit = defineEmits<{ revoke: [session: SessionSummary]; revokeAll: []; retry: [] }>()
</script>
<template>
  <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
    <header class="flex shrink-0 flex-wrap items-center justify-between gap-3 px-6 py-5">
      <div>
        <h2 class="text-[18px] font-semibold">{{ compact ? '登录设备' : '在线会话' }}</h2>
        <p class="mt-1 text-[13px] text-muted">
          {{ sessions.length }} 个有效会话<span v-if="!compact"> · 每账号最多 3 个</span>
        </p>
      </div>
      <ActionBtn
        v-if="canRevokeAll && sessions.length"
        variant="danger"
        icon="log-out"
        :disabled="loading"
        @click="emit('revokeAll')"
        >退出全部会话</ActionBtn
      >
    </header>
    <AsyncContent :loading="loading" :error="error" @retry="emit('retry')">
      <EmptyState
        v-if="!sessions.length"
        title="暂无有效会话"
        desc="该账号当前没有已登录的终端。"
        icon="monitor"
        class="m-5"
      />
      <div v-else-if="compact" class="flex flex-col gap-5 px-6 pb-6">
        <div
          v-for="session in sessions"
          :key="session.session_id"
          class="flex flex-wrap items-center gap-3"
        >
          <AppIcon name="monitor" :size="20" class="text-accent" />
          <div class="min-w-0 flex-1">
            <p class="text-[14px]">{{ session.client }}</p>
            <p class="mt-1 text-[12px] text-muted">
              {{ session.source_ip }} · 最近活动 {{ formatTime(session.last_interactive_at, true) }}
            </p>
          </div>
          <span v-if="session.current" class="rounded bg-ok-soft px-2 py-1 text-[12px] text-ok"
            >当前设备</span
          ><button
            v-else
            type="button"
            class="text-[13px] text-danger"
            @click="emit('revoke', session)"
          >
            退出会话
          </button>
        </div>
      </div>
      <div v-else class="min-h-0 flex-1 overflow-auto">
        <table class="identity-table min-w-[850px]!">
          <thead>
            <tr>
              <th scope="col">登录终端</th>
              <th scope="col">来源地址</th>
              <th scope="col">最近活动</th>
              <th scope="col">空闲到期</th>
              <th scope="col">操作</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="session in sessions" :key="session.session_id">
              <td>
                <p>
                  {{ session.client
                  }}<span v-if="session.current" class="ml-2 text-[12px] text-ok">当前设备</span>
                </p>
                <p class="mt-1 text-[12px] text-muted">
                  登录于 {{ formatTime(session.created_at, true) }}
                </p>
              </td>
              <td>{{ session.source_ip }}</td>
              <td>{{ formatTime(session.last_interactive_at, true) }}</td>
              <td>{{ formatTime(session.idle_expires_at, true) }}</td>
              <td>
                <button
                  type="button"
                  class="text-[13px] text-danger"
                  @click="emit('revoke', session)"
                >
                  退出会话
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </AsyncContent>
    <footer v-if="!compact" class="mt-auto shrink-0 border-t border-line p-6">
      <p class="text-[14px]">退出会话会立即结束对应终端的访问。</p>
      <p class="mt-2 text-[13px] leading-relaxed text-muted">
        后台轮询不会续期。退出他人会话前，需验证你的当前密码。
      </p>
    </footer>
  </section>
</template>
