<script setup lang="ts">
/** 面板容器：6px 圆角 + 1px 描边 + 可选 44px 标题栏 */
withDefaults(
  defineProps<{
    /** 标题栏文案，为空则不渲染标题栏 */
    title?: string
    /** 标题栏右侧说明文字 */
    hint?: string
    /** 面板是否可纵向伸缩 */
    fill?: boolean
    /** 内容区是否去掉内边距 */
    flush?: boolean
  }>(),
  { fill: false, flush: false },
)
</script>

<template>
  <section class="panel flex min-w-0 flex-col overflow-hidden" :class="fill && 'min-h-0 flex-1'">
    <header v-if="title || $slots.actions" class="panel-bar">
      <div class="flex min-w-0 items-baseline gap-3">
        <h3 class="truncate text-[14px] font-semibold text-ink">{{ title }}</h3>
        <span v-if="hint" class="truncate text-[11px] text-muted">{{ hint }}</span>
      </div>
      <div class="flex shrink-0 items-center gap-3">
        <slot name="actions" />
      </div>
    </header>
    <div class="flex min-h-0 flex-1 flex-col" :class="!flush && 'px-4 py-3'">
      <slot />
    </div>
  </section>
</template>
