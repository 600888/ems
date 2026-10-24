<script setup lang="ts">
import zhCn from 'element-plus/es/locale/lang/zh-cn'
import {useThemeStore} from '@/stores/theme'

// 启动即应用主题：设计图深色为默认值
useThemeStore().apply()
</script>

<template>
  <el-config-provider :locale="zhCn" :z-index="3000">
    <router-view />
  </el-config-provider>
</template>
