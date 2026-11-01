<script setup lang="ts">
import { watch } from 'vue'
import { useRouter } from 'vue-router'
import { useMonitorStore } from '@/stores/monitor'
import ActionBtn from '@/components/common/ActionBtn.vue'
const monitor = useMonitorStore(),
  router = useRouter()
watch(
  () => monitor.pages.yc,
  () => {
    void monitor.refresh()
  },
)
</script>
<template>
  <section class="panel flex min-h-0 flex-1 flex-col overflow-hidden">
    <div class="flex items-center justify-between p-4">
      <span class="font-semibold">遥测 · {{ monitor.totals.yc }} 项</span
      ><ActionBtn
        :disabled="!monitor.selectedYcCodes.length"
        @click="router.push('/monitor/curves')"
        >查看选中点历史曲线</ActionBtn
      >
    </div>
    <div class="min-h-0 flex-1 overflow-auto">
      <table class="identity-table">
        <thead>
          <tr>
            <th>曲线</th>
            <th>名称</th>
            <th>编码</th>
            <th>当前值</th>
            <th>单位</th>
            <th>质量</th>
            <th>采集时间</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in monitor.ycList" :key="row.code">
            <td>
              <el-checkbox
                :model-value="monitor.selectedYcCodes.includes(row.code)"
                :disabled="
                  !monitor.selectedYcCodes.includes(row.code) && monitor.selectedYcCodes.length >= 6
                "
                @change="monitor.toggleYc(row.code)"
              />
            </td>
            <td>{{ row.name }}</td>
            <td>{{ row.code }}</td>
            <td class="text-accent">{{ row.value }}</td>
            <td>{{ row.unit }}</td>
            <td>
              {{ row.quality === 'good' ? '有效' : row.quality === 'timeout' ? '超时' : '不可用' }}
            </td>
            <td>{{ row.ts }}</td>
          </tr>
        </tbody>
      </table>
    </div>
    <el-pagination
      v-model:current-page="monitor.pages.yc"
      :page-size="200"
      :total="monitor.totals.yc"
      layout="prev, pager, next"
      class="p-4"
    />
  </section>
</template>
