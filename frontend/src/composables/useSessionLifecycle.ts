import { onBeforeUnmount, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { identityApi } from '@/api/identity'
import { useAuthStore } from '@/stores/auth'

export function useSessionLifecycle() {
  const auth = useAuthStore()
  const router = useRouter()
  let poll = 0,
    expiry = 0,
    lastActivity = 0
  let sending = false
  let channel: BroadcastChannel | undefined
  const end = () => auth.clear('expired')
  async function activity(event: Event) {
    if (!event.isTrusted) return
    if (!auth.full || sending || Date.now() - lastActivity < 30000) return
    sending = true
    lastActivity = Date.now()
    const session = auth.session
    try {
      const value = await identityApi.activity()
      if (auth.session?.csrf_token === session?.csrf_token) auth.apply(value)
    } catch {
      /* HTTP 入口负责过期事件；网络故障不重放用户操作。 */
    } finally {
      sending = false
    }
  }
  watch(
    () => auth.session,
    (session) => {
      window.clearTimeout(expiry)
      if (session) {
        const deadline = Math.min(
          Date.parse(session.idle_expires_at),
          Date.parse(session.absolute_expires_at),
        )
        if (Number.isFinite(deadline))
          expiry = window.setTimeout(end, Math.max(0, deadline - Date.now()))
      } else if (auth.initialized && !router.currentRoute.value.path.startsWith('/login')) {
        void router.replace({
          name: 'login',
          query: auth.endReason ? { reason: auth.endReason } : {},
        })
      }
    },
    { immediate: true },
  )
  onMounted(() => {
    window.addEventListener('ems:session-ended', end)
    window.addEventListener('pointerdown', activity, { passive: true })
    window.addEventListener('keydown', activity)
    poll = window.setInterval(() => {
      if (auth.session && document.visibilityState === 'visible')
        void auth.restore(true).catch(() => undefined)
    }, 30000)
    if ('BroadcastChannel' in window) {
      channel = new BroadcastChannel('ems-session')
      channel.onmessage = () => auth.clear('logout')
    }
  })
  onBeforeUnmount(() => {
    window.clearInterval(poll)
    window.clearTimeout(expiry)
    window.removeEventListener('ems:session-ended', end)
    window.removeEventListener('pointerdown', activity)
    window.removeEventListener('keydown', activity)
    channel?.close()
  })
}
