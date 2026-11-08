# EMS Web 前端

Vue、TypeScript、Pinia 与 Element Plus 实现的站端管理和设备监视界面。页面使用真实 C++ Web API，后台各页面共享统一标题栏。

## 开发与验证

在 `frontend` 目录执行：

```powershell
Copy-Item .env.example .env.local
npm install
npm run dev
npm run build
npm test
```

默认访问 `http://127.0.0.1:5173`（也可使用 `http://localhost:5173`），Vite 将 `/api` 代理到 `.env.local` 中的 `EMS_API_TARGET`（默认 `http://127.0.0.1:8080`），保留原始 Origin。后端 `conf/web.json` 的 `allowed_origins` 已列入这两个开发地址；使用其他域名或端口时同步修改精确允许列表，并重启 Web 使配置生效。

`EMS_IDENTITY_MOCK=false` 是真实对接模式。仅身份界面演示可显式改成 `true`，演示账号见 `.env.example`；设备管理与监视没有模拟数据回退，生产构建不挂载模拟身份服务。

## 字体与字号

全站字体族、字号和正文/表格行距统一在 `src/styles/typography.css` 中维护。默认正文与表格正文为 14px，表头与次要文字为 13px，辅助标注为 12px；表格继续使用紧凑行高。

- 调整全站正文：修改 `--font-size-body`，表格和常规控件默认随之变化。
- 单独调整表格：修改 `--font-size-table` 和 `--font-size-table-head`。
- 调整字体族：修改 `--font-family-sans` 和 `--font-family-mono`，Element Plus 同步使用。
- 页面按用途使用 `text-body`、`text-small`、`text-caption`、`text-section`、`text-title` 等语义类；表格使用 `text-table` / `text-table-head`，表单使用 `text-control` / `text-label`，避免在页面写死像素字号。

## 页面滚动布局

后台与设备监视统一使用 `src/styles/layout.css`：顶部栏和后台导航保持固定，右侧 `workspace-scroll` 是页面纵向滚动容器。正文随内容自然撑开，表格仅保留横向滚动。设备页使用 `device-workspace` / `workspace-content`，左侧 `workspace-sidebar` 设备树吸附在可视区内，超长树节点单独滚动；抽屉与选择器保留自身的滚动行为。

后台工作区顶部使用 `AdminPageHeader` 展示当前路由 `meta.title` 与 `meta.description`，统一呈现页面名称、用途说明和模块图标。标题栏保持可见，正文在下方滚动；设备树可用高度会扣除标题栏高度。

页面级刷新、新建、保存授权等操作通过 `AdminPageActions` 放在标题栏右侧，窄屏下排列在标题下方；查询、重置、行操作和表单提交保留在对应内容区域。`AdminPageActions` 使用 Teleport 保留页面自身的事件处理与权限控制，路由卸载时同步移除按钮。

## 后端配置

从 `backend` 目录启动已构建的 `ems-web --config conf/web.json`。需要先按仓库数据库和身份文档初始化 MySQL 表、身份密钥及管理员；Web 启动不自动建表。数据库连接配置使用 `database_config` 或现有 `EMS_MYSQL_*` 环境变量，身份密钥使用 `identity_key_file` 或 `EMS_WEB_EDIT_KEY_FILE`。

生产部署可在 Web 配置中把 `document_root` 指向 `frontend/dist` 的绝对路径。已知前端页面支持直接访问和刷新，未知 API 仍返回 JSON 404。外部访问须按 Web 配置要求使用 TLS 代理和安全 Cookie。

实时读取需要 `realtime_config` 指向与采集进程一致的实时库配置，`realtime_scope_code` 与采集进程作用域一致。历史查询需要现有历史目录、已编译的 TDengine 驱动和 `EMS_TAOS_HOST`、`EMS_TAOS_PORT`、`EMS_TAOS_USER`、`EMS_TAOS_PASSWORD`、`EMS_TAOS_DATABASE` 配置。凭据不写入前端。

## 已对接功能

- 身份：登录、会话、个人资料、密码、角色、用户权限和登录策略。
- 工程：设备树及档案、设备和部件创建修改删除、全局通信通道、设备绑定、四张点表、固有语义映射、型号模板应用和配置检查。
- 导入：CSV/XLSX 上传、持久任务和分页预览、行修正与确认、校验及应用；整站 ZIP 导入生成分设备任务，进入点表导入页继续确认。
- 监视：实时遥测和遥信、真实采样质量与更新时间、分页；历史曲线和 CSV 导出。
- 运维：审计日志和 Web 系统指标。

遥控、遥调执行按当前需求暂不对接，提交入口禁用。策略、告警等尚无对应页面和服务的导航也禁用，不能产生模拟成功结果。

## 配置与数据规则

配置保存即生效，不使用草稿、发布或配置版本。写请求携带读取表单时的站根 `config_revision`；冲突要求重新加载，不自动覆盖。新建设备、通道和点位默认停用，补齐配置后再启用。消费者比对修订计数后重建运行目录。

实时接口为 `POST /api/monitor/points`；历史接口为 `POST /api/history/query`，支持最多 6 个遥测点、24 小时时间窗口和 10000 行上限。历史按实际时间绘图，超过上限提示截断。无样本显示空值，过期和质量异常不会显示为正常值。实时服务不可用时仍可读取配置点位用于历史查询。

当前验证包含前端构建、API 合约测试、C++ Web 回归测试，以及隔离 SQLite 数据下的真实浏览器保存和刷新回读。实际 MySQL、TDengine、实时库和设备采集链路需在对应服务运行的部署环境联调。
## 清空整站配置

在设备管理工具栏点击“清空整站”，输入当前站编码确认，可同时填写新站名称和编码。清空成功后直接使用“整站导入”加载新配置。该入口仅向拥有全部设备范围及全部工程写权限的用户显示。

操作会删除站内全部下级节点、通道、绑定、四遥点、导入记录及通信统计和状态记录，保留空站入口、账号权限、审计日志、设备类型和型号模板。清空不可撤销；后台在同一事务中检查权限、确认编码和编辑修订号并执行删除，失败时整体回滚。
