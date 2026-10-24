import {computed, ref} from 'vue'
import {defineStore} from 'pinia'

import {bindings, diffRows, importRows, mappingRows, parseIssues} from '@/mocks/config'
import type {BindingRow, DiffRow, ImportRow, MappingRow, ParseIssue} from '@/types/domain'

export type MappingTab = 'all' | 'yc' | 'yx' | 'yk' | 'yt' | 'event'

/** 设备配置流水线：点表导入 → 固有点位映射 → 差异校验（画板 03–06） */
export const useConfigStore = defineStore('config', () => {
  const bindingRows = ref<BindingRow[]>(bindings)
  const importPreview = ref<ImportRow[]>(importRows)
  const issues = ref<ParseIssue[]>(parseIssues)
  const mapping = ref<MappingRow[]>(mappingRows)
  const diffs = ref<DiffRow[]>(diffRows)

  const mappingTab = ref<MappingTab>('all')
  const mappingKeyword = ref('')
  const releaseReason = ref('升级 PCS-125K 点表 v2.3，补充有功反馈与诊断点')
  const releaseStage = ref(1)
  const publishing = ref(false)

  const blockedCount = computed(() => issues.value.length)

  const mappingCounts = computed(() => ({
    all: 28,
    yc: 18,
    yx: 5,
    yk: 4,
    yt: 2,
    event: 1,
  }))

  const filteredMapping = computed(() => {
    const kw = mappingKeyword.value.trim()
    return mapping.value.filter((row) => {
      const hitTab = mappingTab.value === 'all' ? true : row.category === mappingTab.value
      const hitKeyword = !kw || row.name.includes(kw) || row.code.includes(kw)
      return hitTab && hitKeyword
    })
  })

  const enabledCount = computed(() => mapping.value.filter((row) => row.state === 'enabled').length)
  const disabledCount = computed(() => mapping.value.filter((row) => row.state === 'disabled').length)

  function setMappingTab(tab: MappingTab) {
    mappingTab.value = tab
  }

  /** 模拟校验并预览差异：接入后端时替换为 POST /configs/{id}/diff */
  function validateAndDiff() {
    releaseStage.value = 1
    return diffs.value.length
  }

  /** 模拟发布：依次推进四个应用阶段 */
  function publish(onDone?: () => void) {
    publishing.value = true
    releaseStage.value = 2
    window.setTimeout(() => {
      releaseStage.value = 3
      window.setTimeout(() => {
        releaseStage.value = 4
        publishing.value = false
        onDone?.()
      }, 600)
    }, 600)
  }

  return {
    bindingRows,
    importPreview,
    issues,
    mapping,
    diffs,
    mappingTab,
    mappingKeyword,
    releaseReason,
    releaseStage,
    publishing,
    blockedCount,
    mappingCounts,
    filteredMapping,
    enabledCount,
    disabledCount,
    setMappingTab,
    validateAndDiff,
    publish,
  }
})
