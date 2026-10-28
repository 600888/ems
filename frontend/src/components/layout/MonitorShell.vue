<script setup lang="ts">
import AppHeader from '@/components/layout/AppHeader.vue'
</script>
<template>
  <div class="flex h-screen w-full flex-col overflow-hidden bg-surface">
    <AppHeader />
    <main class="flex min-h-0 flex-1 flex-col overflow-hidden px-4 pt-5 pb-6 lg:px-6">
      <router-view />
    </main>
  </div>
</template>
