# 数据库部署入口

应用只检查数据库是否就绪，不在启动时建库、建表或写入示例。

1. 部署账号创建空 MySQL 数据库，选择该库执行 `migrations/000_configuration_mysql.sql`。
2. 存储进程还需执行 `migrations/001_storage_mysql.sql`，TDengine 执行 `migrations/002_storage_taos.sql`。
3. 需要开发演示数据时显式执行 `seeds/collection_demo.sql`，生产不自动执行。
4. 通过 `EMS_DATABASE_CONFIG` 指定配置 JSON，环境变量 `EMS_MYSQL_*` 覆盖文件字段。

`net.sql`、`ems_config.sql`、`point_config_v2.sql` 和 `point_table_excel_migration.sql` 是早期 schema 资料，不作为本次直接替换后的初始化入口。工程配置使用公共 `IConfigurationRepository`；版本化运行配置发布仍需后续实现。
