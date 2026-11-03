<script setup lang="ts">
import { ref } from 'vue'
import TemplateDialog from './TemplateDialog.vue'
import StationImportDialog from './StationImportDialog.vue'
import ActionBtn from '@/components/common/ActionBtn.vue'
import { useAuthStore } from '@/stores/auth'
import { useDeviceStore } from '@/stores/device'

const auth = useAuthStore()
const device = useDeviceStore()
const templates = ref(false),
  stationImport = ref(false)
</script>

<template>
  <div class="flex min-h-10 shrink-0 flex-wrap items-center gap-x-4 gap-y-2">
    <div class="flex min-w-0 flex-1 flex-wrap items-center gap-[7px] text-small">
      <slot />
    </div>
    <div class="ml-auto flex shrink-0 items-center gap-[10px]">
      <ActionBtn icon="layers" @click="templates = true">型号模板</ActionBtn>
      <ActionBtn v-if="auth.can('import.write')" icon="upload" @click="stationImport = true"
        >整站导入</ActionBtn
      >
      <ActionBtn
        v-if="auth.can('device.write')"
        variant="primary"
        icon="plus"
        @click="device.createDrawerOpen = true"
      >
        新建设备
      </ActionBtn>
    </div>
    <TemplateDialog v-model="templates" />
    <StationImportDialog v-model="stationImport" />
  </div>
</template>
