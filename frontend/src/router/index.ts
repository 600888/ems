import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { safeReturnTo } from '@/domain/identity'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/monitor/overview' },
    {
      path: '/login',
      component: () => import('@/components/identity/AuthFrame.vue'),
      meta: { public: true },
      children: [
        { path: '', name: 'login', component: () => import('@/views/identity/LoginView.vue') },
        {
          path: 'change-password',
          name: 'first-password',
          component: () => import('@/views/identity/FirstPasswordView.vue'),
        },
      ],
    },
    {
      path: '/profile',
      component: () => import('@/components/layout/MonitorShell.vue'),
      children: [
        { path: '', name: 'profile', component: () => import('@/views/identity/ProfileView.vue') },
      ],
    },
    {
      path: '/forbidden',
      name: 'forbidden',
      component: () => import('@/views/identity/ForbiddenView.vue'),
    },

    {
      path: '/admin',
      component: () => import('@/components/layout/AdminShell.vue'),
      children: [
        { path: '', redirect: '/admin/devices' },
        {
          path: 'users',
          name: 'users',
          meta: { permission: 'user.read' },
          component: () => import('@/views/identity/UsersView.vue'),
        },
        {
          path: 'users/roles',
          name: 'roles',
          meta: { permission: 'user.read' },
          component: () => import('@/views/identity/RolesView.vue'),
        },
        {
          path: 'users/security-settings',
          name: 'login-policy',
          meta: { permission: 'user.security' },
          component: () => import('@/views/identity/LoginPolicyView.vue'),
        },
        {
          path: 'users/:userId/:tab?',
          name: 'user-detail',
          meta: { permission: 'user.read' },
          component: () => import('@/views/identity/UserDetailView.vue'),
        },
        // 01 层级总览（新建设备抽屉走 ?new=1）
        {
          path: 'devices',
          name: 'admin-devices',
          meta: { permission: 'device.write' },
          component: () => import('@/views/admin/DeviceOverview.vue'),
        },
        // 08 层级详情
        {
          path: 'hierarchy/:id',
          name: 'admin-hierarchy',
          meta: { permission: 'device.write' },
          component: () => import('@/views/admin/HierarchyDetail.vue'),
        },
        // 02–06 共用设备身份与分栏导航
        {
          path: 'devices/:id',
          meta: { permission: 'device.write' },
          component: () => import('@/views/admin/DeviceDetailShell.vue'),
          children: [
            { path: '', redirect: (to) => `/admin/devices/${to.params.id}/basic` },
            {
              path: 'basic',
              name: 'device-basic',
              component: () => import('@/views/admin/BasicInfo.vue'),
            },
            {
              path: 'channels',
              name: 'device-channels',
              component: () => import('@/views/admin/ChannelBinding.vue'),
            },
            {
              path: 'points/import',
              name: 'device-import',
              component: () => import('@/views/admin/PointImport.vue'),
            },
            {
              path: 'points/mapping',
              name: 'device-mapping',
              component: () => import('@/views/admin/PointMapping.vue'),
            },
            {
              path: 'release',
              name: 'device-release',
              component: () => import('@/views/admin/ConfigRelease.vue'),
            },
          ],
        },
      ],
    },

    {
      path: '/monitor',
      meta: { permission: 'monitor.read', resourceScope: true },
      component: () => import('@/components/layout/MonitorShell.vue'),
      children: [
        { path: '', redirect: '/monitor/overview' },
        {
          path: '',
          component: () => import('@/views/monitor/MonitorDeviceShell.vue'),
          children: [
            {
              path: 'overview',
              name: 'monitor-overview',
              component: () => import('@/views/monitor/OverviewView.vue'),
            },
            {
              path: 'telemetry',
              name: 'monitor-telemetry',
              component: () => import('@/views/monitor/TelemetryView.vue'),
            },
            {
              path: 'signals',
              name: 'monitor-signals',
              component: () => import('@/views/monitor/SignalsView.vue'),
            },
            {
              path: 'control',
              name: 'monitor-control',
              meta: { permission: 'command.yk' },
              component: () => import('@/views/monitor/ControlView.vue'),
            },
            {
              path: 'setpoints',
              name: 'monitor-setpoints',
              meta: { permission: 'command.yt' },
              component: () => import('@/views/monitor/SetpointsView.vue'),
            },
            {
              path: 'curves',
              name: 'monitor-curves',
              meta: { permission: 'history.read' },
              component: () => import('@/views/monitor/CurvesView.vue'),
            },
          ],
        },
      ],
    },

    { path: '/:pathMatch(.*)*', redirect: '/monitor/overview' },
  ],
})

router.beforeEach(async (to) => {
  const auth = useAuthStore()
  try {
    await auth.restore()
  } catch {
    return to.name === 'login'
      ? true
      : {
          name: 'login',
          query: {
            reason: 'unavailable',
            ...(safeReturnTo(to.fullPath) ? { return_to: to.fullPath } : {}),
          },
        }
  }
  if (auth.session?.kind === 'password_change')
    return to.name === 'first-password' ? true : { name: 'first-password' }
  if (!auth.full)
    return to.name === 'login'
      ? true
      : {
          name: 'login',
          query: { ...(safeReturnTo(to.fullPath) ? { return_to: to.fullPath } : {}) },
        }
  if (to.path.startsWith('/login')) return auth.landing
  if (typeof to.meta.permission === 'string' && !auth.can(to.meta.permission))
    return { name: 'forbidden' }
  if (to.meta.resourceScope && auth.session?.scope_mode !== 'all' && !auth.session?.scopes?.length)
    return { name: 'profile' }
  return true
})

export default router
