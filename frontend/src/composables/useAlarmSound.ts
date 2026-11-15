import { onBeforeUnmount, ref, watch, type Ref } from 'vue'
import type { AlarmRecord } from '@/types/alarm'

/** 仅在用户主动开启后提示新出现的严重告警，不为历史查询或已有记录反复鸣响。 */
export function useAlarmSound(rows: Ref<AlarmRecord[]>, enabledView: Ref<boolean>) {
  const enabled = ref(false),
    error = ref('')
  let audio: AudioContext | undefined
  let enabledAt = 0
  const seen = new Set<string>()
  async function toggle() {
    error.value = ''
    if (enabled.value) {
      enabled.value = false
      return
    }
    try {
      audio ??= new AudioContext()
      await audio.resume()
      if (audio.state !== 'running') throw new Error('audio_unavailable')
      rows.value.forEach((row) => seen.add(row.alarm_id))
      enabled.value = true
      enabledAt = Date.now()
    } catch {
      error.value = '浏览器无法启用声音，请检查音频设置。'
    }
  }
  watch(rows, (values) => {
    const fresh = values.some(
      (row) =>
        !seen.has(row.alarm_id) &&
        row.occurred_at_ms > enabledAt &&
        row.severity === 'critical' &&
        !row.acknowledgement,
    )
    values.forEach((row) => seen.add(row.alarm_id))
    if (seen.size > 10000) {
      seen.clear()
      values.forEach((row) => seen.add(row.alarm_id))
    }
    if (!fresh || !enabled.value || !enabledView.value || audio?.state !== 'running') return
    const oscillator = audio.createOscillator(),
      gain = audio.createGain(),
      now = audio.currentTime
    oscillator.frequency.value = 720
    gain.gain.setValueAtTime(0.08, now)
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.45)
    oscillator.connect(gain).connect(audio.destination)
    oscillator.start(now)
    oscillator.stop(now + 0.45)
    oscillator.onended = () => {
      oscillator.disconnect()
      gain.disconnect()
    }
  })
  onBeforeUnmount(() => {
    enabled.value = false
    void audio?.close()
  })
  return { enabled, error, toggle }
}
