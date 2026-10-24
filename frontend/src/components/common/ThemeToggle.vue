<script setup lang="ts">
import {computed} from 'vue'
import {useThemeStore} from '@/stores/theme'
import AppIcon from './AppIcon.vue'

const theme = useThemeStore()
const label = computed(() => (theme.mode === 'dark' ? '切换到浅色模式' : '切换到深色模式'))
</script>

<template>
  <el-tooltip :content="label" placement="bottom" :show-after="120">
    <button
      type="button"
      class="flex h-[30px] w-[30px] items-center justify-center rounded-[5px] border border-line bg-raised text-muted transition-colors hover:text-ink"
      :aria-label="label"
      @click="theme.toggle()"
    >
      <AppIcon :name="theme.mode === 'dark' ? 'sun' : 'moon'" :size="15" />
    </button>
  </el-tooltip>
</template>
