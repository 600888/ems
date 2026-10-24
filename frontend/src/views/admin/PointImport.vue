<script setup lang="ts">
import { ElMessage } from 'element-plus'
import { useRouter } from 'vue-router'

import AppIcon from '@/components/common/AppIcon.vue'
import InfoBar from '@/components/common/InfoBar.vue'
import ConfigSteps from '@/components/admin/ConfigSteps.vue'
import { useConfigStore } from '@/stores/config'

const router = useRouter()
const config = useConfigStore()

function saveDraft() {
  ElMessage.success('草稿已保存，未发布前不影响运行')
}

function next() {
  if (config.blockedCount > 0) {
    ElMessage.warning(`尚有 ${config.blockedCount} 项需确认，请修正后继续`)
    return
  }
  router.push('/admin/devices/pcs01/points/mapping')
}
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col gap-4">
    <ConfigSteps :current="2" />
    <InfoBar text="当前目标：pcs01 · 储能变流器 01 ｜ 基础版本 v12 ｜ 本次导入仅生成草稿" />

    <div class="flex min-h-0 flex-1 gap-4">
      <!-- 点表预览 -->
      <section class="panel flex min-w-0 flex-1 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">点表预览与检查</h3>
        </div>

        <div class="flex min-h-0 flex-1 flex-col gap-3 overflow-y-auto p-4">
          <!-- 文件信息 -->
          <div class="flex items-center gap-3 rounded-[6px] border border-line bg-surface p-3">
            <div class="flex h-10 w-10 items-center justify-center rounded-[6px] bg-iconbox">
              <AppIcon name="file-spreadsheet" :size="18" class="text-accent" />
            </div>
            <div class="flex min-w-0 flex-1 flex-col gap-1">
              <span class="truncate text-[13px] font-medium text-ink">PCS-125K_点表_v2.3.xlsx</span>
              <span class="truncate text-[10px] text-muted">
                48.2 KB · 128 行 · 替换草稿 · 地址按协议偏移解析
              </span>
            </div>
            <button
              type="button"
              class="flex h-[34px] shrink-0 items-center gap-[6px] rounded-[5px] border border-line bg-raised px-3 text-[12px] font-medium text-ink"
              @click="ElMessage.info('重新选择文件不在本次示例范围内')"
            >
              <AppIcon name="upload" :size="14" />
              重新选择
            </button>
          </div>

          <!-- 采集绑定 -->
          <div class="grid grid-cols-2 gap-4">
            <div class="flex flex-col gap-[7px]">
              <span class="text-[11px] text-muted">* 实际采集绑定</span>
              <el-select class="ctl-sm w-full" model-value="主采集 ch01 · Unit ID 1">
                <el-option label="主采集 ch01 · Unit ID 1" value="主采集 ch01 · Unit ID 1" />
                <el-option label="控制下发 ch01 · Unit ID 1" value="控制下发 ch01 · Unit ID 1" />
              </el-select>
            </div>
            <div class="flex flex-col gap-[7px]">
              <span class="text-[11px] text-muted">* 地址基准</span>
              <el-select class="ctl-sm w-full" model-value="协议偏移 · 从 0 开始">
                <el-option label="协议偏移 · 从 0 开始" value="协议偏移 · 从 0 开始" />
                <el-option label="PLC 地址 · 从 1 开始" value="PLC 地址 · 从 1 开始" />
              </el-select>
            </div>
          </div>

          <!-- 预览表格 -->
          <div
            class="flex min-h-0 flex-1 flex-col overflow-hidden rounded-[6px] border border-line"
          >
            <div class="flex h-9 shrink-0 items-center justify-between bg-thead px-3">
              <span class="text-[11px] font-medium text-ink">厂家点表预览</span>
              <span class="text-[10px] text-ok"
                >124 行通过 / <span class="text-warn">4 行需确认</span></span
              >
            </div>

            <div class="dt-head">
              <div class="w-10 shrink-0"><span class="dt-th">行</span></div>
              <div class="min-w-0 flex-1"><span class="dt-th">厂家点位</span></div>
              <div class="w-[130px] shrink-0"><span class="dt-th">权限 / 地址</span></div>
              <div class="w-[130px] shrink-0"><span class="dt-th">类型 / 字节序</span></div>
              <div class="w-[110px] shrink-0"><span class="dt-th">单位 / 倍率</span></div>
              <div class="w-[90px] shrink-0"><span class="dt-th">校验</span></div>
            </div>

            <div class="min-h-0 flex-1 overflow-y-auto">
              <div v-for="row in config.importPreview" :key="row.row" class="dt-row h-[40px]">
                <div class="w-10 shrink-0 text-[10px] text-subtle">{{ row.row }}</div>
                <div class="dt-cell min-w-0 flex-1 truncate text-[11px] text-ink">
                  {{ row.name }}
                </div>
                <div class="dt-cell w-[130px] shrink-0 text-[10px] text-muted">
                  {{ row.access }}
                </div>
                <div class="dt-cell w-[130px] shrink-0 text-[10px] text-muted">{{ row.type }}</div>
                <div class="dt-cell w-[110px] shrink-0 text-[10px] text-muted">{{ row.unit }}</div>
                <div class="dt-cell w-[90px] shrink-0">
                  <span
                    class="text-[10px]"
                    :class="row.result === 'pass' ? 'text-ok' : 'text-warn'"
                  >
                    {{ row.result === 'pass' ? '通过' : '待确认' }}
                  </span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

      <!-- 解析检查 -->
      <section class="panel flex w-[332px] shrink-0 flex-col overflow-hidden">
        <div class="panel-bar">
          <h3 class="text-[14px] font-semibold text-ink">解析检查</h3>
          <span class="text-[11px] text-warn">{{ config.blockedCount }} 项需确认</span>
        </div>

        <div class="flex flex-col gap-[14px] overflow-y-auto p-4">
          <div v-for="issue in config.issues" :key="issue.title" class="flex gap-[9px]">
            <AppIcon name="triangle-alert" :size="13" class="mt-[2px] text-warn" />
            <div class="flex flex-col gap-1">
              <span class="text-[11px] font-medium text-ink">{{ issue.title }}</span>
              <span class="text-[10px] text-muted">{{ issue.desc }}</span>
            </div>
          </div>

          <div class="h-px w-full bg-line" />
          <p class="text-[10px] leading-[1.6] text-subtle">
            修正全部必填项后可继续映射。未发布前不影响运行。
          </p>
        </div>
      </section>
    </div>

    <!-- 底部操作 -->
    <div
      class="flex shrink-0 items-center gap-3 rounded-[6px] border border-line bg-panel px-4 py-[10px]"
    >
      <AppIcon name="info" :size="14" :class="config.blockedCount > 0 ? 'text-warn' : 'text-ok'" />
      <span class="text-[11px]" :class="config.blockedCount > 0 ? 'text-warn' : 'text-ok'">
        {{
          config.blockedCount > 0
            ? `尚有 ${config.blockedCount} 项需确认 · 请修正后继续`
            : '解析检查已全部通过'
        }}
      </span>
      <div class="flex-1" />
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink"
        @click="router.back()"
      >
        上一步
      </button>
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-line bg-raised px-[14px] text-[13px] font-medium text-ink"
        @click="saveDraft"
      >
        保存草稿
      </button>
      <button
        type="button"
        class="h-[38px] rounded-[5px] border border-brand bg-brand px-[14px] text-[13px] font-medium text-white transition-all hover:brightness-110"
        @click="next"
      >
        下一步：映射固有点位
      </button>
    </div>
  </div>
</template>
