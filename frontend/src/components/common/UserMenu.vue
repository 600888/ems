<script setup lang="ts">
import {ref} from 'vue'

import AppIcon from '@/components/common/AppIcon.vue'
import type {UserMenuItem} from '@/types/ui'

const props = defineProps<{
  /** 账号显示名，如 值班员 / 管理员 */
  name: string
  /** 账号说明，如 运行值班员 · 华东示范储能站 */
  role: string
  items: UserMenuItem[]
}>()

const emit = defineEmits<{select: [key: string]}>()

const dropdownRef = ref<{handleClose: () => void}>()
const open = ref(false)

function pick(key: string) {
  dropdownRef.value?.handleClose()
  emit('select', key)
}
</script>

<template>
  <el-dropdown
    ref="dropdownRef"
    trigger="click"
    placement="bottom-end"
    popper-class="ems-user-menu"
    :show-arrow="false"
    :offset="6"
    @visible-change="open = $event"
  >
    <button
      type="button"
      class="flex h-8 items-center gap-2 rounded-[5px] px-2 transition-colors"
      :class="open ? 'bg-panel' : 'hover:bg-panel'"
    >
      <AppIcon name="circle-user" :size="24" class="text-accent" />
      <span class="text-[13px] text-ink">{{ props.name }}</span>
      <AppIcon
        :name="open ? 'chevron-up' : 'chevron-down'"
        :size="12"
        class="text-muted transition-transform"
      />
    </button>

    <template #dropdown>
      <div class="w-[248px] rounded-[8px] border border-line bg-raised p-[6px]">
        <div class="flex items-center gap-[9px] px-[10px] py-[10px]">
          <AppIcon name="circle-user" :size="26" class="text-accent" />
          <div class="flex min-w-0 flex-col gap-[3px]">
            <span class="truncate text-[13px] font-semibold text-ink">{{ props.name }}</span>
            <span class="truncate text-[11px] text-muted">{{ props.role }}</span>
          </div>
        </div>

        <div class="h-px w-full bg-line" />

        <button
          v-for="item in props.items"
          :key="item.key"
          type="button"
          class="mt-[2px] flex h-[34px] w-full items-center gap-[9px] rounded-[5px] px-[10px] text-left transition-colors hover:bg-sel"
          @click="pick(item.key)"
        >
          <AppIcon
            :name="item.icon"
            :size="15"
            :class="item.tone === 'danger' ? 'text-danger' : 'text-accent'"
          />
          <span
            class="text-[13px]"
            :class="item.tone === 'danger' ? 'text-danger' : 'text-ink'"
          >
            {{ item.label }}
          </span>
        </button>
      </div>
    </template>
  </el-dropdown>
</template>
