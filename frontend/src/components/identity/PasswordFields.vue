<script setup lang="ts">
import FormField from '@/components/common/FormField.vue'
defineProps<{
  oldPassword: string
  newPassword: string
  confirmation: string
  minimum: number
  first?: boolean
  errors?: Record<string, string>
  horizontal?: boolean
}>()
const emit = defineEmits<{
  'update:oldPassword': [value: string]
  'update:newPassword': [value: string]
  'update:confirmation': [value: string]
}>()
</script>
<template>
  <div class="grid gap-5" :class="horizontal ? 'xl:grid-cols-3' : ''">
    <FormField
      :label="first ? '当前临时密码' : '当前密码'"
      :error="errors?.old"
      required
      v-slot="field"
      ><el-input
        :id="field.id"
        :model-value="oldPassword"
        :aria-invalid="field.invalid"
        :aria-describedby="field.descriptionId"
        type="password"
        show-password
        autocomplete="current-password"
        :placeholder="first ? '请输入当前临时密码' : '请输入当前密码'"
        @update:model-value="emit('update:oldPassword', String($event))"
    /></FormField>
    <FormField label="新密码" :error="errors?.password" required v-slot="field"
      ><el-input
        :id="field.id"
        :model-value="newPassword"
        :aria-invalid="field.invalid"
        :aria-describedby="field.descriptionId"
        type="password"
        show-password
        autocomplete="new-password"
        placeholder="请输入新密码"
        @update:model-value="emit('update:newPassword', String($event))"
    /></FormField>
    <FormField label="确认新密码" :error="errors?.confirmation" required v-slot="field"
      ><el-input
        :id="field.id"
        :model-value="confirmation"
        :aria-invalid="field.invalid"
        :aria-describedby="field.descriptionId"
        type="password"
        show-password
        autocomplete="new-password"
        placeholder="请再次输入新密码"
        @update:model-value="emit('update:confirmation', String($event))"
    /></FormField>
  </div>
  <p class="text-[12px] leading-relaxed text-muted">
    {{ minimum }}–32 个字符，大写、小写、数字、特殊符号中至少三类，且不能与当前密码相同。
  </p>
</template>
