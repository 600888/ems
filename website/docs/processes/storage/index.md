# 存储进程

`ems-storage` 按周期从统一实时库读取当前四遥状态，默认 **每 5 秒存储所有点位**，通过公共时序接口写入 TDengine。设备和测点保留在 `cfg_device` 与四张 `cfg_point_*` 配置表；目录按站根 `config_revision` 热加载，不引入配置版本。详细职责和质量规则见[存储设计](design.md)。

## 配置

`backend/conf/storage.json` 的依赖路径以该配置文件所在目录解析：

```json
{
  "database_config": "db.json",
  "rtdb_config": "realtime.json",
  "timeseries_config": "timeseries.json",
  "scope_code": "station001",
  "interval_ms": 5000
}
```

省略 `interval_ms` 时同样为 5000；支持 1000–3600000 毫秒整数。`scope_code` 与采集进程使用的实时库作用域一致。四遥点位默认都进入周期计划；遥控、遥调只保存已有实时状态，不发起写命令。没有实时样本、停用或陈旧时保存 null 和对应质量原因。

`timeseries.json` 由 Web 与存储共用，填写实际的 TDengine 主机、原生端口、用户和密码；默认库名 `ems_history`。可用 `EMS_TIMESERIES_CONFIG` 指定公共工厂的配置文件，用 `EMS_TAOS_HOST/PORT/USER/PASSWORD/DATABASE` 覆盖连接值。存储通过 `timeseries_config` 显式选择文件，Web 通过自身同名字段选择文件。配置中不写示例真实密码。

## 构建与启动

MySQL 客户端用于配置与稳定序列映射；RTDB 用于实时读取。周期存储不要求开启 ZeroMQ。TDengine C SDK 3.4.3.x 通过 `EMS_DATABASE_TAOS_INCLUDE_DIR` 和 `EMS_DATABASE_TAOS_LIBRARY` 指定，运行时需要对应客户端动态库。没有 SDK 时可编译并检查配置，真实连接会明确返回 Unsupported。

服务端部署后，由部署账号执行 `backend/data/tdengine_history.sql`，创建微秒库及四张超级表；工程关系表继续通过现有 Python 初始化入口管理。应用不在启动时建库或建表。

以下命令以 `backend` 为工作目录，先检查配置，再连接服务：

```powershell
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json --check-config
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json --once
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json
```

`--check-config` 不连接外部服务。`--once` 执行一个实际归档周期，用于部署验收，任意写失败返回非零。持续运行支持 SIGINT / SIGTERM，Windows 另支持 SIGBREAK。

守护配置已带上 `--config conf/storage.json`，存储项已启用。启动需要实时库已经可用，配置目录和时序连接全部就绪后才报告 ready。

## 当前验证边界

前端历史页调用真实 Web 查询接口，Web 直接使用公共历史目录与 TDengine 读取端口，见[历史数据查询](../web/history.md)。测试通过注入实时与时序端口覆盖归档和查询，另有启动配置校验、SQLite 热加载回归与原生 SDK 头文件语法编译验证。

2026-11-08 已完成 TDengine 3.4.3.9 社区版接入，原生数值 / 精确整数 / 布尔 / 中文文本 / null、复合主键、批量写入、游标分页与实际点表的历史查询均已联调。15,617 个配置点位以 5 秒周期完整归档，周期计数可通过 `elapsed_ms`、`written`、`failed` 核对。采集通道尚无有效样本时仍保存 null 与质量原因，不能把这些记录视为有效测量。

Windows 可执行 `scripts/setup_tdengine_dependencies.ps1` 安全解压匹配的官方 SDK。`timeseries.json` 的 `client_config_dir` 指向客户端配置目录；相对路径以连接文件所在目录为基准，`EMS_TAOS_CONFIG_DIR` 可覆盖。中文账户下将进程 TEMP / TMP 指向存在的 ASCII 路径（例如 `backend/tmp`），避免原生 SDK 的默认临时目录编码问题。守护配置已为 Web 和存储设置 `TEMP=tmp` / `TMP=tmp`。

真实集成用例 `TaosLive.*` 仅在显式设置 `EMS_TAOS_LIVE_CONFIG` 时连接服务；工程历史查询还需 `EMS_MYSQL_LIVE_CONFIG`。这两个变量只放配置文件路径，不在命令行传密码。
