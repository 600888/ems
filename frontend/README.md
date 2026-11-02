# EMS Web 前端

Vue、TypeScript、Pinia 与 Element Plus 实现的站端管理和设备监视界面。页面使用真实 C++ Web API，重复的页面标题栏已移除。

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
