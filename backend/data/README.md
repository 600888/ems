# 数据库部署入口

应用只检查数据库是否就绪，不在启动时建库、建表或写入示例。

1. 部署账号创建空 MySQL 数据库，选择该库执行 `migrations/000_configuration_mysql.sql`。
2. 设备管理表执行 `migrations/003_device_management_mysql.sql`（14 张表：设备树 + 四遥四表 + 通道绑定 + 导入 + 运行统计，不含配置版本），再执行 `seeds/device_management_seed.sql`（11 个设备类型 + 47 个固有点）。
3. 存储进程还需执行 `migrations/001_storage_mysql.sql`，TDengine 执行 `migrations/002_storage_taos.sql`。
4. 需要开发演示数据时显式执行 `seeds/collection_demo.sql`，生产不自动执行。
5. 整站点表打包导入（manifest.json + 每设备 xlsx，路径 A）的完整示例见
   `station_package_example/`，打包成 zip 后经 `POST /api/station-imports/create` 上传。
6. 通过 `EMS_DATABASE_CONFIG` 指定配置 JSON，环境变量 `EMS_MYSQL_*` 覆盖文件字段。

工程配置使用公共 `IConfigurationRepository`；设备管理配置的字段契约见 `../../website/docs/common/database/device-management.md`，配置仓储 `IDeviceConfigRepository` 已实现，保存即生效（无发布服务）。早期遗留 schema（`net.sql`、`ems_config.sql`、`point_config_v2.sql`、`point_table_excel_migration.sql`）已于 2026-10-24 清理删除。
