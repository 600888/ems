# 历史数据查询

历史数据页面使用真实设备目录和历史时序接口，支持遥测、遥信、跨设备对比、时间范围、采样间隔、数据明细与 CSV 导出。最多同时选择 6 个测点，停用测点仍可查询已存储的数据。

## 依赖与职责

前端 `api/history.ts` 定义接口契约，`domain/history.ts` 处理时间、质量与 CSV 转义，`stores/history.ts` 管理目录、选择、请求及结果。页面和设备树仅负责交互与展示。

后端 `HistoryService` 独立负责历史查询，通过公共配置仓储验证设备和点位，通过 `IHistoryCatalogRepository` 解析时序映射，通过可注入的 `Reader` 读取 `ITimeSeriesDatabase`。`RuntimeService` 保留历史路由转发，实时监视的既有调用继续兼容。历史服务不依赖实时库，也不改变配置表结构。

查询开始时验证 `history.read`、站范围、设备真实父链及测点归属，再释放配置事务；时序读取期间不持有工程锁。返回前重新建立配置事务，复核当前会话、设备权限及测点归属。停用设备或测点不影响已有历史查询权限。

## 接口契约

`POST /api/history/query`，使用现有 Cookie、Origin 和 CSRF 校验，请求体为 JSON：

```json
{
  "device_id": "pcs01",
  "point_type": "yc",
  "point_ids": ["pcs01-power"],
  "start_ms": 1794009600000,
  "end_ms": 1794096000000,
  "interval_ms": 300000,
  "max_rows": 10000
}
```

| 字段 | 约束 |
| --- | --- |
| `device_id` | 已授权的测点所属设备或部件 ID |
| `point_type` | `yc` 或 `yx`，省略时为 `yc`；分类映射到各自的配置表 |
| `point_ids` | 1–6 个非空 ID，服务端去重；全部必须属于该设备及分类 |
| `start_ms` / `end_ms` | 非负整数 Unix 毫秒，左闭右开，结束晚于开始，跨度最多 30 天 |
| `interval_ms` | `0`（原始数据，默认）、`60000`、`300000`、`900000`、`3600000` |
| `max_rows` | 单次请求最多返回的样本数，1–10000，默认 1000 |

响应使用统一的 `data` / `meta` 包装：

```json
{
  "data": {
    "items": [
      { "point_id": "pcs01-power", "timestamp": 1794009600000, "value": 12.5, "quality_code": 0 }
    ],
    "truncated": false
  },
  "meta": { "config_revision": "42" }
}
```

原始数据保留样本毫秒时间。非零间隔以请求开始时间对齐时间桶，每个测点、每个桶保留最后一个实际样本及其质量码，响应时间为桶起点。无数据的桶不补值，不以前一个桶的数据推测当前状态。图表、明细和 CSV 使用同一份返回数据；布尔遥信显示为 0 / 1，文本遥信保留在明细中，无效质量在图表中形成断点。

服务端从设备真实祖先推导站、最近单元两个存储作用域，使用严格递增游标每批最多读取 1001 条，按原始时间、测点 ID、事件 ID 归并去重后采样。每次查询最多消费 200000 条原始记录，并受请求期限约束；预读缓冲最多为每个作用域一批。达到结果上限或扫描预算时返回 `truncated=true`，扫描未完成的时间桶不输出。前端按设备和分类分组请求，每组最多返回 10000 条；页面明确提示缩小范围，CSV 只导出已返回的数据。

| 情况 | 结果 |
| --- | --- |
| 参数或范围不合法 | 400，`history_bounds` / `history_point_type` |
| 无历史读取权限 | 403；站范围或设备不可见按现有资源权限规则拒绝 |
| 点位分类错误、点位不存在或归属其他设备 | 404，不读取时序库 |
| 配置点位存在，但尚无时序映射或样本 | 成功返回空集 |
| 时序连接未配置、客户端未编译或服务未就绪 | 503，`history_service_unavailable` |
| 存储返回越界、错误映射或非递增记录 | 503，`history_scope_mismatch` / `history_order_mismatch` |
| 底层读取超期 | 按公共错误映射返回超时，不能作为完整结果导出 |

改变查询条件或选择会清空旧结果并取消在途请求；新查询和账号切换会使旧响应失效。部分设备请求失败时保留成功组的数据，并显示失败设备及分类。

## 部署与验证

Web 与存储均通过共享 `timeseries.json` 的 `backend` 选择 TAOS 或 InfluxDB 2.9。InfluxDB 使用仓库内 `influxdb-cxx` 的兼容接口，配置 `url`、`database`（默认 DBRP 映射名）、`token` 和 `timeout_ms`；部署须先建立 bucket 和 DBRP 映射，切换后重启两个进程。配置示例及依赖说明见[切换 InfluxDB 2.9](../storage/index.md#切换-influxdb-29)。下述 TAOS SDK 要求仅适用于 `backend=taos`。

构建时需要公共时序模块检测到 `taos.h` 和 `taos` 库（可指定 `EMS_DATABASE_TAOS_INCLUDE_DIR`、`EMS_DATABASE_TAOS_LIBRARY`），现有适配器要求 TDengine 3.4.3 客户端及 `ems_history` 数据库。运行环境配置 `EMS_TAOS_HOST`（默认 `127.0.0.1`）、`EMS_TAOS_PORT`（默认 `6030`）、`EMS_TAOS_USER`、`EMS_TAOS_PASSWORD`、`EMS_TAOS_DATABASE`（默认 `ems_history`）。Web 与存储通过 `timeseries_config` 共用 `backend/conf/timeseries.json`，环境变量可覆盖凭据。周期归档默认每 5 秒写入所有四遥状态，部署入口及建库脚本见[存储进程](../storage/index.md)；未配置时页面显示明确服务错误。

前端关闭 `EMS_IDENTITY_MOCK`，通过现有同源 Vite / HTTPS 代理访问 Web。运行 `npm test`、`npm run build`；后端构建 `test_web`、`ems-web`，运行 `WebHistory.*` 和 `WebDevice.History*`，覆盖遥信、30 天范围、质量码、跨作用域采样、分页、越权、参数校验及扫描预算。前端测试还覆盖目录完整分页、子设备保留、请求竞态、账号退出、部分失败、时间更新和 CSV 转义。真实 TDengine 连接、存储数据及现场链路需在部署环境联调。
