import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/', redirect: '/monitor/overview' },

    {
      path: '/admin',
      component: () => import('@/components/layout/AdminShell.vue'),
      children: [
        { path: '', redirect: '/admin/devices' },
        // 01 层级总览（新建设备抽屉走 ?new=1）
        {
          path: 'devices',
          name: 'admin-devices',
          component: () => import('@/views/admin/DeviceOverview.vue'),
        },
        // 08 层级详情
        {
          path: 'hierarchy/:id',
          name: 'admin-hierarchy',
          component: () => import('@/views/admin/HierarchyDetail.vue'),
        },
        // 02–06 共用设备身份与分栏导航
        {
          path: 'devices/:id',
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
              component: () => import('@/views/monitor/ControlView.vue'),
            },
            {
              path: 'setpoints',
              name: 'monitor-setpoints',
              component: () => import('@/views/monitor/SetpointsView.vue'),
            },
            {
              path: 'curves',
              name: 'monitor-curves',
              component: () => import('@/views/monitor/CurvesView.vue'),
            },
          ],
        },
      ],
    },

    { path: '/:pathMatch(.*)*', redirect: '/monitor/overview' },
  ],
})

export default router
