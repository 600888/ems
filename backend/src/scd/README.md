# ems-storage

此目录只保留 C++17 存储业务编排。历史目录/事件接口位于 `backend/include/common/database/history/`，公共 ormpp 实现位于 `backend/src/common/db/mysql/`；时序接口、类型与工厂位于 `common/database/timeseries/`，TDengine 3.4.2 客户端封装位于 `src/common/db/timeseries/taos/`。`influxdb` 仍返回 `Unsupported`。

存储入口现已通过 `ZmqChangeSubscriber` 订阅完整 ZeroMQ 主题，接收 `<topic><NUL><EventEnvelope JSON>` 单帧消息。接收回调将载荷放入有界内存队列，工作循环解析、校验、按 MySQL 点位版本查找映射并写入 TDengine。Redis 不参与该进程的数据传输。PUB/SUB 没有投递确认或重放：采集端、网络、接收队列或数据库故障时均可能丢样，存储进程不能用实时最新值补造历史。MySQL 事件身份账本和隔离记录仅处理**已经到达存储进程**的事件。

采集端尚未发布上述完整遥测信封；因此存储入口已能订阅，但还没有端到端采样生产链路。外部 HistoryService RPC、配置发布、outbox 目标适配、聚合与故障验收也尚未接入。`IHistoryFeed::read` 取出消息后不保留副本，单条处理失败计入 `dropped`，不会自动重试；`FeedStatistics` 只统计本进程已接收以及本地队列溢出或关闭时丢弃，不能计量网络或发布端丢样。

## 构建

已完成[公共数据库访问层设计](../../../website/docs/common/database-design.md)的首批迁移：移除本目录私有 MySQL/TAOS 实现，入口通过 `connect_mysql_database` 创建共享池的目录/事件 Repository，并注入消费者。目录核验与事件身份登记使用同一显式事务，提交确认后释放 MySQL 连接再写时序库；提交 Unknown 时不发送样本。隔离使用新短事务，历史查询在时序扫描期间不持有 MySQL 事务。

`ems_storage_core` 仅依赖公共 C++17 API。`ems_database_runtime` 仅适配显式事务，`ems_database_mysql` 复用 ormpp 连接池并实现仓储；缺客户端时工厂返回 Unsupported。每个进程独立初始化库的进程内单例池，进程内相同配置复用公共上下文；不同配置返回 Conflict。池随进程存活，不跨进程共享连接，也不另建池队列。原生进程仍需 MySQL 客户端和 `EMS_WITH_ZEROMQ=ON`，TDengine 需 3.4.2 SDK；配置可用 `EMS_DATABASE_MYSQL_INCLUDE_DIR/LIBRARY`、`EMS_DATABASE_TAOS_INCLUDE_DIR/LIBRARY` 指定依赖。客户端头和驱动链接仅在公共实现侧使用，`native_adapters.h` 仅作类型别名过渡。

MySQL 默认最多 4 条连接、借用等待 1 秒、每次网络 I/O 上限 5 秒；公共配置可调整。网络超时按客户端整秒精度取当前剩余预算与配置上限，不能提供亚秒强制取消。事务归创建线程，跨上下文、跨线程或已关闭事务拒绝；失败连接丢弃，重新借用时创建新连接，不自动重放事务。公共 MySQL 适配器仅为会话选项/错误诊断调用 C API，业务 CRUD 使用 ormpp。迁移脚本仍由部署执行，进程仅检查所需历史列，不自动改表；完整 schema 版本管理与真实服务故障验收待后续完成。

目标 TDengine 服务端与客户端均为 3.4.2 系列，并使用微秒精度的 `ems_history` 库。先在目标补丁版本验证迁移 SQL、复合键和 stmt2 行为，再导入真实历史点位版本。`history_point_version.ready_at` 只应在该版本全部快照核对完成后设置。

## 事件契约与环境变量

| 名称 | 含义 |
| --- | --- |
| `EMS_DATABASE_CONFIG` | 可选公共 MySQL 配置 JSON 文件，环境变量覆盖文件字段 |
| `EMS_MYSQL_HOST`, `EMS_MYSQL_PORT`, `EMS_MYSQL_USER`, `EMS_MYSQL_PASSWORD`, `EMS_MYSQL_DATABASE` | MySQL 连接 |
| `EMS_STORAGE_SERIES_BACKEND` | `taos` 或 `tdengine`；默认 `taos`。`influxdb` 当前返回 `Unsupported` |
| `EMS_TAOS_HOST`, `EMS_TAOS_PORT`, `EMS_TAOS_USER`, `EMS_TAOS_PASSWORD` | TDengine 原生连接；选择 TDengine 时使用 |
| `EMS_STORAGE_ZMQ_ENDPOINT` | ZeroMQ SUB 连接端点，默认 `tcp://127.0.0.1:5559` |
| `EMS_STORAGE_SCOPE_KIND`、`EMS_STORAGE_SCOPE_ID` | 作用域类型 `station`/`unit` 与稳定 ID；单进程实例订阅一个作用域 |
| `EMS_STORAGE_ZMQ_TOPIC` | 完整订阅主题，默认 `ems:telemetry:<kind>:<id>`；发布端须精确匹配 |

ZeroMQ 遥测正文为公共 `EventEnvelope` JSON。telemetry `payload` 至少包含 `point_id`、`source_id`、十进制字符串 `source_epoch` 与 `sequence`、`value_type`、`value`、整数 `quality`。`value_type` 可为 `numeric`、`integer`、`boolean`、`text`；64 位整数值必须是十进制字符串。无效值使用 `null` 并填写 `quality_reason`。完整事件的规范化 JSON 计算 SHA-256；同一 `event_id` 的任何重发内容不得变化。

存储 C++ 接口使用 `series_key` 表示序列映射；当前 ormpp 仓储从 MySQL 物理列 `history_point_series.series_table` 读取并映射到该字段，TDengine 适配器只接受 `p_` 加小写十六进制的内部子表名。未来 InfluxDB 适配器需要新的受控映射，不能直接沿用 TAOS 子表规则。已就绪版本中的 `history_point_snapshot` 与映射须先由配置发布流程填充；缺失版本的已收到样本无法依靠 PUB/SUB 重投，不能回退到当前点位配置。隔离记录由人工复核后处理。

## 本地验证

2026-10-22：MSVC Release 后端构建及 200 项 CTest 通过（含新增工程配置事务回归）；其中直接使用 ormpp 池的运行时与池恢复测试共 14 项，也在 MinGW C++17 下通过。MySQL 适配器已用 MariaDB Connector/C 3.4.8 编译链接，4 项无服务测试验证参数/期限拒绝、ORM 线程错误隔离及 uint64 绑定。当前未安装 TAOS SDK，原生 ZeroMQ 进程未参与本轮链接；测试结果不代表真实数据库和遥测链路验收。

`backend/tests/storage/test_storage.cpp` 覆盖已取出样本写入失败后的丢弃、身份冲突、版本未就绪、隔离提交失败，以及跨作用域查询、分页和 outbox 场景。这些核心测试不证明 ZeroMQ 链路的投递率或故障表现。当前工作区没有完成采集发布端与 MySQL、TDengine、ZeroMQ 的端到端联调；配置发布、HistoryService gRPC 和丢样监控仍需验收。
