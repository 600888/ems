import { fileURLToPath, URL } from 'node:url'

import tailwindcss from '@tailwindcss/vite'
import vue from '@vitejs/plugin-vue'
import { defineConfig, loadEnv } from 'vite'
import { identityMock } from './dev/identity-mock.ts'

export default defineConfig(({ command, mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const mock = command === 'serve' && env.EMS_IDENTITY_MOCK === 'true'
  return {
    plugins: [vue(), tailwindcss(), ...(mock ? [identityMock()] : [])],
    resolve: {
      alias: {
        '@': fileURLToPath(new URL('./src', import.meta.url)),
      },
    },
    server: {
      port: 5173,
      host: '127.0.0.1',
      proxy: {
        '/api': { target: env.EMS_API_TARGET || 'http://127.0.0.1:8080', changeOrigin: false },
      },
    },
  }
})
