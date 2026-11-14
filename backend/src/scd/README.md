# ems-storage

历史存储采用周期归档：读取当前设备配置与统一实时库，所有四遥点位默认每 5000 毫秒存储一次。遥控 / 遥调仅归档已有状态，不执行命令。无实时样本、停用、陈旧或非法值写入 null 和明确质量原因，不以零代替。

- `sampling.h/.cpp`：目录构建、按站根 `config_revision` 热加载、周期采样与质量处理。
- `storage_options.h/.cpp`：启动配置与相对路径解析。
- `main.cpp`：依赖装配、定时调度、退出与失败重连。
- 公共 MySQL 仓储拥有稳定序列映射；公共 TDengine 适配器拥有参数绑定及游标扫描。配置保存即生效，不创建配置版本或配置快照。
- 历史子表名取站根至测点的各级 `code`，以 `:` 连接，例如 `station:unit01:bms01:rack01:voltage`；最长 192 字节。编码或父级改变时需显式重建历史映射，不能复用旧哈希表名。

启动配置为 `backend/conf/storage.json`，默认 `interval_ms=5000`，连接参数共用 `timeseries.json`。`backend=taos` 使用 TDengine；`backend=influxdb` 使用仓库内 `influxdb-cxx` 接入 InfluxDB 2.9 的兼容接口，填写 `url`、`database`（默认 DBRP 映射名）、`token` 和可选 `timeout_ms`，切换后重启 Web 与存储。InfluxDB bucket 与 DBRP 由部署创建，操作示例见[存储进程文档](../../../website/docs/processes/storage/index.md#切换-influxdb-29)。MySQL 仍使用公共 `db.json`，RTDB 与采集进程保持相同配置和作用域。周期入口不依赖 ZeroMQ；既有事件消费和 outbox 核心保留为独立能力，不参与定时归档。

构建目标为 `ems-storage`。MySQL 客户端和 RTDB 为必要依赖；真正连接 TDengine 需要 3.4.3.x C SDK 及匹配动态库。建库脚本为 `backend/data/tdengine_history.sql`，应用不执行 DDL。

从 `backend` 运行：

```powershell
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json --check-config
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json --once
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json
```

`--check-config` 不连接外部服务；`--once` 用于部署后验证单周期落库。持续运行不回填停机或错过的采样槽；时序写失败在后续周期重连，报告真实写入 / 无效 / 失败计数。

完整设计、表字段、质量语义与部署边界见 [存储进程文档](../../../website/docs/processes/storage/index.md) 和 [详细设计](../../../website/docs/processes/storage/design.md)。2026-11-08 已在 TDengine 3.4.3.9 社区版完成原生类型读写、批量写入、游标分页、真实点表归档及 Web 历史服务联调；采集通道未就绪的点仍按无效质量保存。

## Windows SDK 与联调

从仓库根目录执行 `./scripts/setup_tdengine_dependencies.ps1`，只解压官方 3.4.3.9 客户端到构建缓存，并输出 CMake 参数。SDK、解压工具均固定摘要，不执行安装器，不修改系统目录；依赖 Python 3。

Web / 存储共用 `timeseries.json` 的 `client_config_dir`，指定 `conf/tdengine/taos.cfg` 所在目录。Windows 中文账户的原生客户端临时目录需要使用 ASCII 路径；守护配置为两个进程设置 `TEMP=tmp`、`TMP=tmp`，启动前确保工作目录下存在 `tmp`。手工启动时可将 TEMP / TMP 指向 `backend/tmp` 的绝对路径。

配置构建后，显式设置 `EMS_TAOS_LIVE_CONFIG` 和 `EMS_MYSQL_LIVE_CONFIG` 为连接配置绝对路径，再运行 `test_taos_live`。未设置时测试跳过；设置后测试会创建并清理随机测试点的子表，工程点查询测试只读现有归档。
