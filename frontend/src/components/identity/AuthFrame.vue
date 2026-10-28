<script setup lang="ts">
import AppIcon from '@/components/common/AppIcon.vue'
const features = [
  { icon: 'monitor', title: '运行监视', text: '实时四遥、告警与历史曲线' },
  { icon: 'sliders-horizontal', title: '运行操作', text: '按权限提交遥控、遥调与告警处置' },
  { icon: 'shield-check', title: '账号安全', text: '账号访问受控，关键操作保留审计记录' },
]
</script>
<template>
  <div class="flex min-h-screen flex-col bg-surface">
    <header
      class="flex h-[88px] shrink-0 items-center justify-between gap-4 border-b border-line bg-nav px-6 lg:px-12"
    >
      <div class="flex items-center gap-[10px]">
        <AppIcon name="zap" :size="26" class="text-accent" /><span class="text-[23px] font-bold"
          >EMS</span
        ><span class="text-[15px] text-muted">能源管理系统</span>
      </div>
      <div class="hidden items-center gap-3 text-[14px] text-muted sm:flex">
        <AppIcon name="building-2" :size="17" />华东示范储能站
      </div>
    </header>
    <main class="flex flex-1">
      <aside
        class="hidden w-[52.083%] flex-col gap-8 border-r border-line bg-nav px-16 py-[88px] lg:flex 2xl:px-[100px] 2xl:py-[104px]"
      >
        <p class="flex items-center gap-3 text-[15px] font-medium text-accent">
          <AppIcon name="radio" :size="20" />站端工作台
        </p>
        <h1 class="text-[36px] leading-[1.5] font-semibold 2xl:text-[44px]">
          华东示范储能站<br />能源管理系统
        </h1>
        <p class="max-w-[660px] text-[17px] leading-relaxed text-muted">
          登录后，按账号角色与设备授权范围访问运行数据和管理功能。
        </p>
        <div class="flex flex-col gap-6 py-6">
          <div v-for="feature in features" :key="feature.title" class="flex items-start gap-[18px]">
            <div class="flex h-11 w-11 shrink-0 items-center justify-center rounded-[6px] bg-panel">
              <AppIcon :name="feature.icon" :size="21" class="text-accent" />
            </div>
            <div>
              <h2 class="text-[16px] font-medium">{{ feature.title }}</h2>
              <p class="mt-1 text-[14px] text-muted">{{ feature.text }}</p>
            </div>
          </div>
        </div>
        <p class="mt-auto pt-10 text-[13px] text-muted">仅限授权人员使用 · 请勿共享个人账号</p>
      </aside>
      <section class="flex min-w-0 flex-1 items-center justify-center px-6 py-12 lg:p-14">
        <div class="w-full max-w-[440px]"><router-view /></div>
      </section>
    </main>
    <footer
      class="flex min-h-14 shrink-0 flex-wrap items-center justify-between gap-2 border-t border-line px-6 py-4 text-[12px] text-muted lg:px-12"
    >
      <span>EMS · 华东示范储能站</span><span class="text-subtle">访问异常请联系站内运维人员</span>
    </footer>
  </div>
</template>
