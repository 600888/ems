<script setup lang="ts">
import { computed, nextTick, ref, watch, useId } from 'vue'
import type { ElTree } from 'element-plus'
import AppIcon from '@/components/common/AppIcon.vue'
import type { DeviceNode } from '@/types/domain'
import type { ResourceScope, RoleCode } from '@/types/identity'
import { scopesFromKeys } from '@/domain/identity'

const props = defineProps<{
  modelValue: ResourceScope[]
  scopeMode: 'all' | 'selected'
  role: RoleCode
  tree: DeviceNode[]
  disabled?: boolean
}>()
const emit = defineEmits<{
  'update:modelValue': [value: ResourceScope[]]
  'update:scopeMode': [value: 'all' | 'selected']
}>()
const treeRef = ref<InstanceType<typeof ElTree>>()
const keyword = ref('')
const groupName = useId()
const all = computed(() => props.scopeMode === 'all')
watch(
  [() => props.modelValue, () => props.tree],
  async () => {
    await nextTick()
    treeRef.value?.setCheckedKeys(
      props.modelValue.map((scope) => scope.device_root_id ?? scope.station_id),
    )
  },
  { deep: true, immediate: true },
)
watch(keyword, (value) => treeRef.value?.filter(value))
function changeMode(mode: 'all' | 'selected') {
  emit('update:scopeMode', mode)
  if (mode === 'all') emit('update:modelValue', [])
}
function onCheck(_node: DeviceNode, info: { checkedKeys: (string | number)[] }) {
  emit('update:modelValue', scopesFromKeys(props.tree, info.checkedKeys.map(String)))
}
</script>
<template>
  <div class="flex min-h-0 flex-col gap-4">
    <div class="flex flex-wrap items-center justify-between gap-3">
      <h3 class="text-[17px] font-semibold">设备授权范围</h3>
      <span class="text-[13px] text-accent">{{
        all ? '全部站与设备' : `已选择 ${modelValue.length} 个范围`
      }}</span>
    </div>
    <div class="flex flex-wrap gap-x-6 gap-y-2 text-[14px]">
      <label class="flex items-center gap-2"
        ><input
          type="radio"
          :name="groupName"
          :checked="!all"
          :disabled="disabled"
          class="accent-accent"
          @change="changeMode('selected')"
        />指定站 / 设备子树</label
      >
      <label class="flex items-center gap-2" :class="role !== 'admin' && 'text-subtle'"
        ><input
          type="radio"
          :name="groupName"
          :checked="all"
          :disabled="disabled || role !== 'admin'"
          class="accent-accent"
          @change="changeMode('all')"
        />全部站与设备<span class="text-[12px] text-subtle">（仅管理员）</span></label
      >
    </div>
    <template v-if="!all">
      <el-input
        v-model="keyword"
        placeholder="搜索站、单元或设备"
        clearable
        aria-label="搜索设备授权范围"
        ><template #prefix><AppIcon name="search" /></template
      ></el-input>
      <div
        class="max-h-[360px] min-h-[220px] overflow-y-auto rounded-[5px] border border-line bg-surface px-3 py-2"
      >
        <el-tree
          ref="treeRef"
          :data="tree"
          node-key="id"
          show-checkbox
          default-expand-all
          :expand-on-click-node="false"
          :props="{ label: 'name', children: 'children', disabled: () => Boolean(disabled) }"
          :filter-node-method="
            (value: string, data: DeviceNode) =>
              !value || `${data.name} ${data.code}`.toLowerCase().includes(value.toLowerCase())
          "
          @check="onCheck"
        >
          <template #default="{ data }"
            ><span class="flex min-w-0 items-center gap-2 text-[14px]"
              ><AppIcon
                :name="data.icon === 'container' ? 'boxes' : data.icon"
                class="text-muted"
              /><span class="truncate">{{ data.name }}</span></span
            ></template
          >
        </el-tree>
      </div>
      <p class="text-[12px] leading-relaxed text-muted">
        勾选设备子树包含其全部下级。祖先仅用于导航，不自动获得访问权限。未选择范围的账号仅可使用个人中心。
      </p>
    </template>
    <p
      v-else
      class="rounded-[5px] border border-line bg-surface p-5 text-[13px] leading-relaxed text-muted"
    >
      此账号可访问当前系统管理的全部站和设备，仍受角色操作权限与安全联锁约束。
    </p>
  </div>
</template>
