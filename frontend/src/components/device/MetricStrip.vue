<script setup lang="ts">
import MetricCards from '@/components/common/MetricCards.vue'
import { useMonitorStore } from '@/stores/monitor'
const monitor = useMonitorStore()
</script>
<template><MetricCards :items="monitor.profile.metrics" class="min-h-[88px]" /></template>
