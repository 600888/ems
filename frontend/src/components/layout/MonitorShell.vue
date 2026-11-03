<script setup lang="ts">
import AppHeader from '@/components/layout/AppHeader.vue'
</script>
<template>
  <div class="app-shell">
    <AppHeader />
    <main class="workspace-scroll">
      <router-view />
    </main>
  </div>
</template>
