# EMS 前端（前台设备监视 + 后台设备管理）

依据 `.pen/device_manage.pen` 设计稿实现的 14 个业务画板，界面结构、文案与配色对齐设计图。

## 技术栈

| 项 | 版本 |
| --- | --- |
| Vue | 3.5.44（`<script setup>` + TypeScript） |
| Vite | 8.3.5 |
| Element Plus | 2.14.7（按需注册，见 `src/main.ts`） |
| Tailwind CSS | 4.3.3（`@tailwindcss/vite`） |
| Pinia | 4.1.0 |
| Vue Router | 5.4.0 |
| 图标 | `@lucide/vue`（与设计稿的 lucide 名称一一对应） |

## 启动

```bash
cd frontend
npm install
npm run dev        # http://127.0.0.1:5173
npm run build      # 类型检查 + 生产构建
npm run typecheck
```

## 页面与路由

| 画板 | 路由 | 文件 |
| --- | --- | --- |
| 01 设备管理 · 层级总览 | `/admin/devices` | `views/admin/DeviceOverview.vue` |
| 02 设备详情 · 基本信息 | `/admin/devices/:id/basic` | `views/admin/BasicInfo.vue` |
| 03 设备接入 · 通道绑定 | `/admin/devices/:id/channels` | `views/admin/ChannelBinding.vue` |
| 04 点表导入 · 预览与校验 | `/admin/devices/:id/points/import` | `views/admin/PointImport.vue` |
| 05 固有点位 · 映射与能力 | `/admin/devices/:id/points/mapping` | `views/admin/PointMapping.vue` |
| 06 配置发布 · 差异与应用 | `/admin/devices/:id/release` | `views/admin/ConfigRelease.vue` |
| 07 新建设备 · 接入抽屉 | `/admin/devices?new=1` | `components/admin/NewDeviceDrawer.vue` |
| 08 层级详情 | `/admin/hierarchy/:id` | `views/admin/HierarchyDetail.vue` |
| 10 设备监视 · 运行总览 | `/monitor/overview` | `views/monitor/OverviewView.vue` |
| 11 设备监视 · 遥测 | `/monitor/telemetry` | `views/monitor/TelemetryView.vue` |
| 12 设备监视 · 遥信 | `/monitor/signals` | `views/monitor/SignalsView.vue` |
| 13 设备监视 · 遥控与确认 | `/monitor/control` | `views/monitor/ControlView.vue` |
| 14 设备监视 · 遥调与校验 | `/monitor/setpoints` | `views/monitor/SetpointsView.vue` |
| 15 设备监视 · 实时曲线 | `/monitor/curves` | `views/monitor/CurvesView.vue` |

画板 09（后台视觉规范）与 16（前台交互与边界状态）按要求未做成页面，其规范已落到组件与状态样式里（按钮层级、状态标签、加载/失败/空态、操作反馈）。画板 17 / 18（前台、后台顶栏的用户菜单展开态）落到 `components/common/UserMenu.vue`，由 `MonitorShell` / `AdminShell` 共用。

## 前后台分工

- **前台设备监视**：顶栏右侧依次为站点选择、活动告警、采集状态、时间、消息、用户；页面不再有独立标题栏（原 94px 的「设备监视 / 实时掌握设备运行状态与四遥数据」已删除），内容区上内边距 20px。
- **用户菜单**：点击用户区展开，前台首项为「后台管理」（进 `/admin/devices`），后台首项为「返回前台」（回 `/monitor/overview`），末项均是「退出登录」。
- **后台只做配置**：设备管理列表、设备详情、通道绑定、层级详情均不展示采集数据。列表用「固有点位 / 配置版本」与「配置状态（已生效 / 有草稿）」；详情用「配置摘要（固有点位 / 映射覆盖率 / 待发布草稿）」；通道改用「配置状态（已启用）」与「通信参数（功能码 / 响应超时 / 重试次数）」；层级子节点用「映射点位 / 点表模板 / 配置状态」。运行数据只在前台设备监视出现。

## 主题

- 深色为设计稿基准，默认启用；顶栏右侧可切换浅色，选择写入 `localStorage`。
- 也支持 URL 直达：`/admin/devices?theme=light`、`?theme=dark`。
- 令牌定义在 `src/styles/tokens.css`，Tailwind 语义色在 `src/styles/main.css` 的 `@theme inline` 中映射；
  Element Plus 变量在 `src/styles/element.css` 中对齐同一套令牌。

| 令牌 | 深色（设计稿） | 浅色 |
| --- | --- | --- |
| 主背景 | `#09192D` | `#EEF2F7` |
| 导航 | `#061426` | `#FFFFFF` |
| 面板 | `#0F253F` | `#FFFFFF` |
| 边框 | `#243D59` | `#D8E1EC` |
| 正文 | `#EBF4FF` | `#0D1B2E` |
| 次级文字 | `#9BB0C8` | `#5A7089` |
| 强调 | `#55D9EF` | `#0E8FA6` |
| 主操作 | `#287BFF` | `#1D6FE8` |
| 正常 / 告警 / 故障 | `#48D8AD` / `#F5BC62` / `#FF7883` | `#169E7C` / `#B87A16` / `#DD4B58` |

## 数据

当前全部为 mock，未接入后端：

- `src/api/device.ts`：设备树与列表接口（含模拟延时），接真实后端时替换为 HTTP 请求。
- `src/mocks/`：设备树、设备列表、四遥点位、通道绑定、点表预览、映射、差异预览。
- `src/stores/`：`device` / `monitor` / `config` / `theme` 四个 Pinia store，页面状态、筛选、选中、操作流程都在这里。
- 遥控、遥调、发布为本地模拟流程（延时 + 结果态），刷新后回到初始状态。

## 与设计的已知差异

- 画板 01 的设备表格列宽已按设计稿重排，并在「设备名称」列使用自适应填充，避免右侧留白。
- 曲线为手写 SVG（`components/charts/LineChart.vue`），按设计稿的网格与断点规则绘制，不引入图表库。
- 字体优先使用本机 `Noto Sans SC`，缺失时回退 `PingFang SC` / `Microsoft YaHei`，不加载外部字体。
- 顶栏保留主题切换按钮（设计稿无此控件），属于本地开发期的便捷入口。
