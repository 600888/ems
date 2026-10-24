<script setup lang="ts">
import { computed, reactive } from 'vue'
import { ElMessage } from 'element-plus'

import AppIcon from '@/components/common/AppIcon.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import { deviceTypeOptions } from '@/mocks/devices'
import { useDeviceStore } from '@/stores/device'

const device = useDeviceStore()

const form = reactive({
  kind: '通信设备',
  name: '储能变流器 02',
  code: 'pcs02',
  type: 'PCS · 储能变流器',
  parent: '储能单元 01',
  vendor: '示例厂商 A',
  model: 'PCS-125K',
  firmware: 'FW 2.1.6',
  power: '125 kW',
  template: '复用型号模板 · PCS-125K / v2.3',
  desc: '',
})

const codeValid = computed(() => form.code.trim().length > 0)

function close() {
  device.createDrawerOpen = false
}

function submit() {
  if (!form.name.trim() || !form.code.trim()) {
    ElMessage.warning('请填写设备名称与设备编码')
    return
  }
  ElMessage.success(`已创建 ${form.code}，下一步配置通道与点表`)
  close()
}
</script>

<template>
  <el-drawer
    v-model="device.createDrawerOpen"
    :with-header="false"
    size="560px"
    class="ems-drawer"
    append-to-body
  >
    <div class="flex h-full flex-col">
      <!-- 抽屉标题 -->
      <header class="flex items-start justify-between border-b border-line px-5 py-[18px]">
        <div class="flex flex-col gap-[5px]">
          <h2 class="text-[20px] leading-none font-semibold text-ink">新建设备</h2>
          <p class="text-[11px] text-muted">在当前设备层级下创建设备</p>
        </div>
        <button type="button" class="text-muted transition-colors hover:text-ink" @click="close">
          <AppIcon name="x" :size="18" />
        </button>
      </header>

      <!-- 表单区 -->
      <div class="flex min-h-0 flex-1 flex-col gap-[18px] overflow-y-auto px-5 py-4">
        <InfoBar text="所属路径：华东示范储能站 / 储能单元 01" />

        <div class="flex flex-col gap-[7px]">
          <span class="text-[11px] text-muted">节点性质 *</span>
          <div class="flex gap-2">
            <button
              v-for="option in ['通信设备', '层级节点']"
              :key="option"
              type="button"
              class="h-[34px] rounded-[5px] border px-4 text-[12px] font-medium transition-colors"
              :class="
                form.kind === option
                  ? 'border-accent bg-sel text-accent'
                  : 'border-line bg-raised text-muted hover:text-ink'
              "
              @click="form.kind = option"
            >
              {{ option }}
            </button>
          </div>
        </div>

        <el-form label-position="top" class="drawer-form">
          <div class="grid grid-cols-2 gap-4">
            <el-form-item label="设备名称 *">
              <el-input v-model="form.name" class="ctl" placeholder="请输入设备名称" />
            </el-form-item>
            <el-form-item label="设备编码 *">
              <el-input v-model="form.code" class="ctl" placeholder="请输入设备编码" />
            </el-form-item>
          </div>

          <div class="grid grid-cols-2 gap-4">
            <el-form-item label="设备类型 *">
              <el-select v-model="form.type" class="ctl w-full">
                <el-option
                  v-for="item in deviceTypeOptions"
                  :key="item"
                  :label="item"
                  :value="item"
                />
              </el-select>
            </el-form-item>
            <el-form-item label="父级节点 *">
              <el-select v-model="form.parent" class="ctl w-full">
                <el-option label="储能单元 01" value="储能单元 01" />
                <el-option label="储能单元 02" value="储能单元 02" />
                <el-option label="站级公共设备" value="站级公共设备" />
              </el-select>
            </el-form-item>
          </div>

          <div class="grid grid-cols-2 gap-4">
            <el-form-item label="设备厂家">
              <el-input v-model="form.vendor" class="ctl" />
            </el-form-item>
            <el-form-item label="设备型号">
              <el-input v-model="form.model" class="ctl" />
            </el-form-item>
          </div>

          <div class="grid grid-cols-2 gap-4">
            <el-form-item label="固件版本">
              <el-input v-model="form.firmware" class="ctl" />
            </el-form-item>
            <el-form-item label="额定有功功率">
              <el-input v-model="form.power" class="ctl" />
            </el-form-item>
          </div>
        </el-form>

        <p v-if="codeValid" class="flex items-center gap-[6px] text-[10px] text-ok">
          <AppIcon name="circle-check" :size="13" />
          {{ form.code }} 在当前父节点下可用；发布后编码保持稳定
        </p>

        <div class="h-px w-full bg-line" />

        <div class="flex flex-col gap-[7px]">
          <span class="text-[11px] text-muted">点表初始化</span>
          <el-select v-model="form.template" class="ctl w-full">
            <el-option
              label="复用型号模板 · PCS-125K / v2.3"
              value="复用型号模板 · PCS-125K / v2.3"
            />
            <el-option
              label="复用型号模板 · BMS-215 / v1.8"
              value="复用型号模板 · BMS-215 / v1.8"
            />
            <el-option label="不初始化，稍后导入点表" value="不初始化，稍后导入点表" />
          </el-select>
          <p class="text-[10px] text-subtle">模板仅复用点表与映射，实际通信通道需单独绑定。</p>
        </div>

        <div class="flex flex-col gap-[7px]">
          <span class="text-[11px] text-muted">设备描述</span>
          <el-input
            v-model="form.desc"
            type="textarea"
            :rows="3"
            resize="none"
            placeholder="填写安装位置或设备用途（选填）"
          />
        </div>

        <div class="flex-1" />

        <InfoBar text="创建后继续配置通道、导入点表并完成发布。" />
      </div>

      <!-- 抽屉操作 -->
      <footer class="flex items-center justify-end gap-[10px] border-t border-line px-5 py-3">
        <button
          type="button"
          class="h-[38px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink transition-colors hover:border-subtle"
          @click="close"
        >
          取消
        </button>
        <button
          type="button"
          class="flex h-[38px] items-center gap-[7px] rounded-[5px] border border-brand bg-brand px-[14px] text-[13px] font-medium text-white transition-all hover:brightness-110"
          @click="submit"
        >
          创建并配置
        </button>
      </footer>
    </div>
  </el-drawer>
</template>
