# ems-storage

历史存储采用周期归档：读取当前设备配置与统一实时库，所有四遥点位默认每 5000 毫秒存储一次。遥控 / 遥调仅归档已有状态，不执行命令。无实时样本、停用、陈旧或非法值写入 null 和明确质量原因，不以零代替。

- `sampling.h/.cpp`：目录构建、按站根 `config_revision` 热加载、周期采样与质量处理。
- `storage_options.h/.cpp`：启动配置与相对路径解析。
- `main.cpp`：依赖装配、定时调度、退出与失败重连。
- 公共 MySQL 仓储拥有稳定序列映射；公共 TDengine 适配器拥有参数绑定及游标扫描。配置保存即生效，不创建配置版本或配置快照。

启动配置为 `backend/conf/storage.json`，默认 `interval_ms=5000`，连接参数共用 `timeseries.json`。MySQL 仍使用公共 `db.json`，RTDB 与采集进程保持相同配置和作用域。周期入口不依赖 ZeroMQ；既有事件消费和 outbox 核心保留为独立能力，不参与定时归档。

构建目标为 `ems-storage`。MySQL 客户端和 RTDB 为必要依赖；真正连接 TDengine 需要 3.4.2.x C SDK 及匹配动态库。建库脚本为 `backend/data/tdengine_history.sql`，应用不执行 DDL。

从 `backend` 运行：

```powershell
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json --check-config
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json --once
./build/windows-msvc/bin/Release/ems-storage.exe --config conf/storage.json
```

`--check-config` 不连接外部服务；`--once` 用于部署后验证单周期落库。持续运行不回填停机或错过的采样槽；时序写失败在后续周期重连，报告真实写入 / 无效 / 失败计数。

完整设计、表字段、质量语义与部署边界见 [存储进程文档](../../../website/docs/processes/storage/index.md) 和 [详细设计](../../../website/docs/processes/storage/design.md)。本轮尚未部署 TDengine 服务端，不代表真实落库与现场链路已验收。
