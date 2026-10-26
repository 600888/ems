# 数据库部署入口

应用只检查数据库是否就绪，不在启动时建库、建表或写入示例。

1. 关系数据库初始化入口为 `backend/python/db` 的 SQLAlchemy 声明式 ORM 与 Alembic。
   在仓库根目录安装 `python -m pip install -e ./backend/python`，按需安装 `[mysql]` 或
   `[postgresql]` 驱动，编辑 `backend/python/database.yaml` 选择数据库及连接参数。
   YAML 仅供迁移工具使用；配置和命令详见 [Python 迁移说明](../python/README.md)。
2. MySQL / PostgreSQL 先创建空数据库，再运行 `ems-db init`，统一建立现有 19 张关系表；
   默认 SQLite 在 `backend/python/ems.sqlite3` 建库。`ems-db seed catalog` 显式写入站根、
   11 个设备类型与 47 个固有点；`ems-db check` 检查结构差异。
3. 原建表与种子 SQL 脚本已移除，不再手动执行 SQL 初始化关系表。
   需要开发演示数据时显式执行 `ems-db seed demo`，生产不自动执行。
   TDengine 历史库不在当前 SQLAlchemy 迁移范围内，需根据存储设计由部署单独准备。
4. 整站点表打包导入（manifest.json + 每设备 xlsx，路径 A）的完整示例见
   `station_package_example/`，打包成 zip 后经 `POST /api/station-imports/create` 上传。
5. C++ 运行时仍通过 `EMS_DATABASE_CONFIG` 指定 MySQL 配置 JSON，环境变量 `EMS_MYSQL_*`
   覆盖文件字段；Python 工具另外支持迁移专用 YAML 与 `EMS_DATABASE_URL`。

设备、通道、绑定及四遥配置统一使用 `IDeviceConfigRepository` 访问 `cfg_*` 表。
采集进程也读取同一套配置；点表导入使用 Web 设备管理入口或上述整站导入接口。
旧设备、通道、测点表及旧点表 CLI 已移除。
