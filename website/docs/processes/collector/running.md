# 采集进程构建与运行

`ems-collector` 是独立的常驻采集入口，复用现有 `CollectEngine`、`CfgLoader`、协议插件和统一实时库。
`EMS_BUILD_COLLECTOR` 默认开启，各平台预设也显式启用；`BUILD_EXAMPLES=OFF` 不影响该进程。

设备、通道、绑定和测点统一来自 `cfg_device`、`cfg_channel`、`cfg_binding`、
`cfg_point_yc/yx/yk/yt`；先按 [数据库初始化说明](https://github.com/600888/ems/blob/main/backend/python/README.md) 建表。
组件点使用 `source_device_id` 指定的通信设备绑定，禁用节点、通道或绑定会停止相关采集。
读数按点配置执行倍率、偏移或取反，实时库键使用稳定 `point_id`，四遥种类由所属表决定。
控制命令按稳定 `point_id` 解析至遥控/遥调地址，支持遥控固定值与遥调反向换算。
进程每秒轮询新表，站根 `config_revision` 变化后重建通道；读取或配置解析失败时保留已运行目录。
当前 Modbus 插件支持 bool、i16/u16、i32/u32、f32/f64，i64/u64 配置会明确报错。
DLT698 插件每通道使用一个表地址；DLT645 当前只编译遥测路由。

## 构建

依赖与后端公共模块一致：MySQL/MariaDB 客户端、hiredis、Boost 头文件及启用的协议栈。
仅构建采集进程时，可关闭 Web 来省去 Drogon、Trantor、JsonCpp 和 zlib 的框架依赖：

```powershell
cd backend
cmake --preset windows-msvc -DEMS_BUILD_WEB=OFF
cmake --build --preset windows-msvc --target ems-collector
```

构建该目标也会构建已启用的 Modbus、IEC104、DLT645、DLT698 插件。
正式程序输出到构建目录的 `bin/`，协议插件位于 `lib/`；MSVC 在两者下追加 `<配置>/`。
Windows 构建后会将已声明的 DLL 依赖复制到 EXE 所在目录。
MariaDB 客户端连接 MySQL 8 时还需构建并部署 `caching_sha2_password.dll`；
CMake 从客户端库附近自动查找认证插件，也可用 `EMS_DATABASE_MYSQL_PLUGIN_DIR` 指定目录，
随后与客户端 DLL 一起复制到程序目录。

## 配置与启动

默认配置文件为 `conf/collector.json`，相对于进程工作目录。部署时应以构建或安装根目录作为工作目录。

```json
{
  "database_config": "db.json",
  "rtdb_config": "realtime.json",
  "scope_code": "station001",
  "poll_interval_ms": 1000,
  "max_channels": 256
}
```

`database_config`、`rtdb_config` 和可选的 `plugin_dir` 相对于采集配置文件所在目录解析。
未提供 `plugin_dir` 时按可执行文件位置定位同一构建目录的 `lib/`，也支持 MSVC 多配置目录。
不指定 `database_config` 时使用公共 `EMS_DATABASE_CONFIG` / `EMS_MYSQL_*` 配置；
`EMS_MYSQL_*` 也可覆盖数据库文件字段。进程入口不另存数据库凭据。

`scope_code` 为实时库写入作用域。`poll_interval_ms` 允许 1～3,600,000 毫秒，
实际下限由通道线程保证；`max_channels` 允许 1～65,536，超过上限拒绝启动，避免静默遗漏通道。

在仓库根目录可显式指定配置路径执行：

```powershell
./backend/build/windows-msvc/bin/Release/ems-collector.exe --help
./backend/build/windows-msvc/bin/Release/ems-collector.exe --config backend/conf/collector.json --check-config
./backend/build/windows-msvc/bin/Release/ems-collector.exe --config backend/conf/collector.json
```

`--check-config` 校验入口参数、数据库配置、实时库文件和插件目录是否存在，不连接数据库、实时库或现场设备。
正常启动读取数据库已有配置并启动启用通道，持续运行直到收到 Ctrl+C / SIGTERM，再等待通道线程退出并释放插件。
任一通道无法初始化时清理已启动通道并返回非零退出码；设备暂时离线由原通道重连逻辑处理。
进程使用 `conf/log.json` 中的 `ems-collector` 模块记录日志。

## 当前边界

该入口接入现有配置加载、YC/YX 采集和实时库写入，不包含完整目标设计中的配置 revision 轮询、
在线重建运行目录、北向上送、可靠命令消费和历史遥测发布。配置修改后当前入口需重启以重新加载。
可选的旁路变更通知默认关闭，启动不依赖通知 Redis 或 ZeroMQ。
真实 MySQL schema、实时库作用域与现场协议连接仍需按部署环境验证，构建成功不代表现场已就绪。
