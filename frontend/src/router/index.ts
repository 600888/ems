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
          path: 'channels',
          name: 'channels',
          meta: {
            permission: 'channel.write',
            title: '通信配置',
            description: '管理采集通道、连接参数与通信启用状态。',
          },
          component: () => import('@/views/admin/ChannelsView.vue'),
        },
        {
          path: 'templates',
          name: 'templates',
          meta: {
            permission: 'device.read',
            title: '型号与点表',
            description: '查看设备类型与型号模板，统一维护设备点位定义。',
          },
          component: () => import('@/views/admin/TemplatesView.vue'),
        },
        {
          path: 'audit',
          name: 'audit',
          meta: {
            permission: 'audit.read',
            title: '审计日志',
            description: '查询配置变更与账号操作记录，追踪站端管理活动。',
          },
          component: () => import('@/views/admin/SystemView.vue'),
        },
        {
          path: 'system',
          name: 'system',
          meta: {
            permission: 'system.read',
            title: '系统指标',
            description: '查看 Web 服务运行状态与请求处理指标。',
          },
          component: () => import('@/views/admin/SystemView.vue'),
        },
        {
          path: 'users',
          name: 'users',
          meta: {
            permission: 'user.read',
            title: '用户管理',
            description: '管理站内账号、角色授权与设备访问范围。',
          },
          component: () => import('@/views/identity/UsersView.vue'),
        },
        {
          path: 'users/roles',
          name: 'roles',
          meta: {
            permission: 'user.read',
            title: '角色权限',
            description: '查看内置角色的功能权限与操作边界。',
          },
          component: () => import('@/views/identity/RolesView.vue'),
        },
        {
          path: 'users/security-settings',
          name: 'login-policy',
          meta: {
            permission: 'user.security',
            title: '登录保护',
            description: '统一设置账号保护、会话安全与密码规则。',
          },
          component: () => import('@/views/identity/LoginPolicyView.vue'),
        },
        {
          path: 'users/:userId/:tab?',
          name: 'user-detail',
          meta: {
            permission: 'user.read',
            title: '用户详情',
            description: '查看账号资料，调整角色授权并管理账号安全。',
          },
          component: () => import('@/views/identity/UserDetailView.vue'),
        },
        // 01 层级总览（新建设备抽屉走 ?new=1）
        {
          path: 'devices',
          name: 'admin-devices',
          meta: {
            permission: 'device.write',
            title: '设备管理',
            description: '维护站、单元、设备与部件，查看设备配置状态。',
          },
          component: () => import('@/views/admin/DeviceOverview.vue'),
        },
        // 08 层级详情
        {
          path: 'hierarchy/:id',
          name: 'admin-hierarchy',
          meta: {
            permission: 'device.write',
            title: '层级详情',
            description: '查看当前节点档案、下级设备与关联点位。',
          },
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
              meta: {
                title: '基本信息',
                description: '维护当前设备的名称、厂家、型号与启用状态。',
              },
              component: () => import('@/views/admin/BasicInfo.vue'),
            },
            {
              path: 'channels',
              name: 'device-channels',
              meta: {
                title: '通道绑定',
                description: '配置当前设备的采集通道、通信地址与接入能力。',
              },
              component: () => import('@/views/admin/ChannelBinding.vue'),
            },
            {
              path: 'points/import',
              name: 'device-import',
              meta: { title: '点表导入', description: '上传设备点表，预览、修正并确认点位配置。' },
              component: () => import('@/views/admin/PointImport.vue'),
            },
            {
              path: 'points/mapping',
              name: 'device-mapping',
              meta: {
                title: '四遥点位',
                description: '维护当前设备的遥测、遥信、遥控与遥调点位。',
              },
              component: () => import('@/views/admin/PointMapping.vue'),
            },
            {
              path: 'release',
              name: 'device-release',
              meta: {
                title: '配置检查',
                description: '检查设备、通道与点位配置，定位需要补充的问题。',
              },
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

    // 30 前台 · 历史数据（独立页面，测点树勾选查询）
    {
      path: '/history',
      component: () => import('@/components/layout/MonitorShell.vue'),
      meta: { permission: 'history.read' },
      children: [
        {
          path: '',
          name: 'history',
          component: () => import('@/views/history/HistoryView.vue'),
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
