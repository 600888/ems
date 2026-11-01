<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import { useRoute } from 'vue-router'
import { deviceApi } from '@/api/device'
import { errorMessage } from '@/api/http'
import ActionBtn from '@/components/common/ActionBtn.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import type { Finding } from '@/types/engineering'
const route = useRoute(),
  id = computed(() => String(route.params.id)),
  busy = ref(false),
  error = ref(''),
  findings = ref<Finding[]>([]),
  checked = ref(false)
async function load() {
  busy.value = true
  error.value = ''
  checked.value = false
  try {
    const r = await deviceApi.validate(id.value)
    findings.value = r.issues
    checked.value = true
  } catch (e) {
    error.value = errorMessage(e)
  } finally {
    busy.value = false
  }
}
watch(id, load, { immediate: true })
</script>
<template>
  <section class="panel flex min-h-0 flex-col gap-5 overflow-auto p-6">
    <div class="flex items-center justify-between">
      <h3 class="text-[17px] font-semibold">配置检查</h3>
      <ActionBtn :loading="busy" @click="load">重新检查</ActionBtn>
    </div>
    <InfoBar v-if="error" :text="error" tone="danger" /><InfoBar
      v-else-if="checked"
      :text="findings.length ? `发现 ${findings.length} 项配置问题` : '配置检查通过'"
      :tone="findings.length ? 'warn' : 'ok'"
    />
    <p class="text-[13px] text-muted">
      设备、通道和点位保存后立即生效；采集服务检测配置修订变化后自动重载。
    </p>
    <div v-for="(f, i) in findings" :key="i" class="border-b border-line py-3">
      <p class="text-warn">{{ f.reason }}</p>
      <p class="mt-2 text-[12px] text-muted">{{ f.device_id }} {{ f.point_id }}</p>
    </div>
  </section>
</template>
