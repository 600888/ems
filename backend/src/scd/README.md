# ems-storage

此目录实现 C++17 存储进程的历史采样路径。MySQL 通过 ormpp 读取已就绪的历史点位目录并登记不可变事件身份；TDengine 3.4.2 通过 stmt2 写入和读取类型化超级表；Redis telemetry Stream 使用 `history-storage` 消费组。TAOS 写入确认或 MySQL 隔离记录提交之前不会 XACK。历史查询与 outbox 发布的应用层逻辑在 `ems_storage_core`，外部 RPC、配置快照发布和 outbox 目标适配仍需接入。

## 构建

`ems_storage_core` 只依赖仓库公共 C++17 模块。安装 MySQL 客户端开发库、TDengine 3.4.2 C/C++ 客户端与 hiredis 后，CMake 自动构建 `ems-storage`。MySQL 仓储使用仓库内固定的 ormpp 0.2.0，不直接调用 MySQL C API。`backend/data/migrations/001_storage_mysql.sql` 和 `002_storage_taos.sql` 必须由部署迁移流程执行；进程不会自动改表。

目标 TDengine 服务端与客户端均为 3.4.2 系列，并使用微秒精度的 `ems_history` 库。先在目标补丁版本验证迁移 SQL、复合键和 stmt2 行为，再导入真实历史点位版本。`history_point_version.ready_at` 只应在该版本全部快照核对完成后设置。

## 环境变量

| 名称 | 含义 |
| --- | --- |
| `EMS_MYSQL_HOST`, `EMS_MYSQL_PORT`, `EMS_MYSQL_USER`, `EMS_MYSQL_PASSWORD`, `EMS_MYSQL_DATABASE` | MySQL 连接 |
| `EMS_TAOS_HOST`, `EMS_TAOS_PORT`, `EMS_TAOS_USER`, `EMS_TAOS_PASSWORD` | TDengine 原生连接 |
| `EMS_REDIS_HOST`, `EMS_REDIS_PORT`, `EMS_REDIS_USER`, `EMS_REDIS_PASSWORD` | Redis 连接；未启用 ACL 时用户名可留空 |
| `EMS_STORAGE_STREAM_KEY` | 完整 telemetry Stream 键，例如 `ems:{ess000001}:stream:telemetry` |
| `EMS_STORAGE_SCOPE_ID` | 事件内稳定 station/unit ID，不是 Redis 键中的 code |
| `EMS_STORAGE_CONSUMER_ID` | 实例名加启动代次；同组实例不得共用 |

一个进程实例消费一个作用域 Stream。生产者的 Stream entry 使用 `event` 字段，值为公共 `EventEnvelope` JSON；其中 telemetry `payload` 至少包含 `point_id`、`source_id`、十进制字符串 `source_epoch` 与 `sequence`、`value_type`、`value`、整数 `quality`。`value_type` 可为 `numeric`、`integer`、`boolean`、`text`；64 位整数值必须是十进制字符串。无效值使用 `null` 并填写 `quality_reason`。完整事件的规范化 JSON 计算 SHA-256，重试不得变更同一 `event_id` 的任何字段。

MySQL `history_point_series.series_table` 只接受 `p_` 加小写十六进制的内部表名。已就绪版本中的 `history_point_snapshot` 与映射必须先由配置发布流程填充；缺失版本保留 Stream pending，不回退到当前点位配置。隔离记录由人工复核后处理。

## 本地验证

`backend/tests/storage/test_storage.cpp` 覆盖 TAOS 失败后重试、身份冲突、版本未就绪、隔离提交失败、原始坏消息隔离、跨作用域查询拒绝、分页和 outbox 未确认发布。现有 Windows 工作区未安装可链接的 MySQL/TDengine/hiredis 客户端，也没有运行中的三库服务，因此原生进程的联调、配置发布、HistoryService gRPC 和故障恢复验收尚未完成。
