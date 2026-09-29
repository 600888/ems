# ems-storage

此目录放置 C++17 存储进程的私有头文件和实现；只有跨进程调用所需的契约放在 `backend/include/common/`。MySQL 通过 ormpp 读取历史点位版本并登记已收到事件的不可变身份。时序读写使用 `ITimeSeriesDatabase` 抽象，当前实现 TDengine 3.4.2；`influxdb` 配置目前返回 `Unsupported`，待选定版本和数据模型后实现。

存储入口现已通过 `ZmqChangeSubscriber` 订阅完整 ZeroMQ 主题，接收 `<topic><NUL><EventEnvelope JSON>` 单帧消息。接收回调将载荷放入有界内存队列，工作循环解析、校验、按 MySQL 点位版本查找映射并写入 TDengine。Redis 不参与该进程的数据传输。PUB/SUB 没有投递确认或重放：采集端、网络、接收队列或数据库故障时均可能丢样，存储进程不能用实时最新值补造历史。MySQL 事件身份账本和隔离记录仅处理**已经到达存储进程**的事件。

采集端尚未发布上述完整遥测信封；因此存储入口已能订阅，但还没有端到端采样生产链路。外部 HistoryService RPC、配置发布、outbox 目标适配、聚合与故障验收也尚未接入。`IHistoryFeed::read` 取出消息后不保留副本，单条处理失败计入 `dropped`，不会自动重试；`FeedStatistics` 只统计本进程已接收以及本地队列溢出或关闭时丢弃，不能计量网络或发布端丢样。

## 构建

`ems_storage_core` 只依赖仓库公共 C++17 模块。原生进程需要 MySQL 客户端开发库和 libzmq 4.3+；启用 TDengine 后端还需要 3.4.2 C/C++ 客户端。配置时设置 `EMS_WITH_ZEROMQ=ON`，CMake 才会创建 `ems_zmq_messaging` 和存储原生目标。MySQL 仓储使用仓库内固定的 ormpp 0.2.0，自有代码不直接调用 MySQL C API。`backend/data/migrations/001_storage_mysql.sql` 和 `002_storage_taos.sql` 由部署迁移流程执行；进程不会自动改表。

目标 TDengine 服务端与客户端均为 3.4.2 系列，并使用微秒精度的 `ems_history` 库。先在目标补丁版本验证迁移 SQL、复合键和 stmt2 行为，再导入真实历史点位版本。`history_point_version.ready_at` 只应在该版本全部快照核对完成后设置。

## 事件契约与环境变量

| 名称 | 含义 |
| --- | --- |
| `EMS_MYSQL_HOST`, `EMS_MYSQL_PORT`, `EMS_MYSQL_USER`, `EMS_MYSQL_PASSWORD`, `EMS_MYSQL_DATABASE` | MySQL 连接 |
| `EMS_STORAGE_SERIES_BACKEND` | `taos` 或 `tdengine`；默认 `taos`。`influxdb` 当前返回 `Unsupported` |
| `EMS_TAOS_HOST`, `EMS_TAOS_PORT`, `EMS_TAOS_USER`, `EMS_TAOS_PASSWORD` | TDengine 原生连接；选择 TDengine 时使用 |
| `EMS_STORAGE_ZMQ_ENDPOINT` | ZeroMQ SUB 连接端点，默认 `tcp://127.0.0.1:5559` |
| `EMS_STORAGE_SCOPE_KIND`、`EMS_STORAGE_SCOPE_ID` | 作用域类型 `station`/`unit` 与稳定 ID；单进程实例订阅一个作用域 |
| `EMS_STORAGE_ZMQ_TOPIC` | 完整订阅主题，默认 `ems:telemetry:<kind>:<id>`；发布端须精确匹配 |

ZeroMQ 遥测正文为公共 `EventEnvelope` JSON。telemetry `payload` 至少包含 `point_id`、`source_id`、十进制字符串 `source_epoch` 与 `sequence`、`value_type`、`value`、整数 `quality`。`value_type` 可为 `numeric`、`integer`、`boolean`、`text`；64 位整数值必须是十进制字符串。无效值使用 `null` 并填写 `quality_reason`。完整事件的规范化 JSON 计算 SHA-256；同一 `event_id` 的任何重发内容不得变化。

存储 C++ 接口使用 `series_key` 表示序列映射；当前 ormpp 仓储从 MySQL 物理列 `history_point_series.series_table` 读取并映射到该字段，TDengine 适配器只接受 `p_` 加小写十六进制的内部子表名。未来 InfluxDB 适配器需要新的受控映射，不能直接沿用 TAOS 子表规则。已就绪版本中的 `history_point_snapshot` 与映射须先由配置发布流程填充；缺失版本的已收到样本无法依靠 PUB/SUB 重投，不能回退到当前点位配置。隔离记录由人工复核后处理。

## 本地验证

`backend/tests/storage/test_storage.cpp` 覆盖已取出样本写入失败后的丢弃、身份冲突、版本未就绪、隔离提交失败，以及跨作用域查询、分页和 outbox 场景。这些核心测试不证明 ZeroMQ 链路的投递率或故障表现。当前工作区没有完成采集发布端与 MySQL、TDengine、ZeroMQ 的端到端联调；配置发布、HistoryService gRPC 和丢样监控仍需验收。
