# 统一实时数据库模块 rtdb

`ems_rtdb` 是共享库，`ems-rtdb` 是链接该库的常驻初始化入口。消费进程包含
`<rtdb/client/realtime_client.h>` 并链接 `ems::rtdb`，不需要第三方 SDK 头文件。

本批实现记录级 read、CAS、条件删除、逐项批量及 JSON 后端配置。Boost.Interprocess
负责共享段、allocator/string/map 和命名锁，redis-plus-plus 负责 Redis 连接池、Hash
和 eval。自有代码不实现原生共享内存、socket、RESP 或跨进程分配器。

业务测点统一使用 `PointClient`，底层通过 `RealtimeService` 读写同一套 `Point` 记录。
采集、Web 监控、归档和北向上送共用设备路径与测点编码，不再保留 business 命名空间映射、
旧测点缓存或第二套控制队列。旧 `RtDb`、`RtDbManager`、`RTDBEMS` 及对应模型已删除。

类型化门面 `ems::rtdb::RealtimeService`（`rtdb/client/realtime_service.h`）在同一动态库内
提供测点/设备/通道状态、来源授权核对、序号与重复判断、新鲜度、作用域配置版本记录和
能力运行条件。`configuration_catalog()` 从当前设备配置编译运行目录，以站根
`config_revision` 判断配置是否仍有效。采集启动与热加载时安装新的来源 epoch，旧样本保留，
在新样本到达前按来源失效处理。管理 RPC、可信部署发现仍待实现。

```text
backend/include/rtdb/         公共头，命名空间 ems::rtdb
  client/                    记录、测点与命令客户端
  model/                     运行目录与设备配置投影
  store/                     记录契约和后端工厂
  backend/                   后端打开与维护接口
backend/src/rtdb/
  client/                    业务序列化与有界 CAS 更新
  model/                     运行目录编译
  store/                     记录校验、编码与版本工具
  backend/                   系统 Boost.Interprocess / redis-plus-plus
  process/                   ems-rtdb 常驻入口
backend/tests/rtdb/           后端、业务、采集分发器、安装包消费测试
```

原 realtime 公共头路径和命名空间已统一为 rtdb，调用方须重新编译。

## 控制命令接口

`<rtdb/client/command_client.h>` 提供 `CommandClient::open`、`submitCommand`、
`readCommand`、`requestVerification`、`listCommands` 和执行者用的 `updateCommand`。
调用方先将命令目标、值、期限和站根 `config_revision` 写入独立 `CommandState` 记录，
再由库内部异步发布 `command.pending` 提示；通知正文只有命令 ID。
每个 ID 独立保存，命令目标不可修改，revision 用于 CAS 认领，实测值继续由采集更新。

Web、命令行、北向控制均使用这一入口，采集进程不再开放命令 REQ/REP 端点。
采集侧接到提示后从实时库读取，并以有界扫描补偿丢通知；执行前保留 MySQL 条件登记，
执行结果同步写回独立命令记录及 YK/YT 测点投影，失败时重试补同步，重启后的未决动作
只核验、不盲目重发。YK/YT 值表示命令目标并附带命令状态；设备实际状态仍读取 YC/YX。
反馈核验读取的新样本先写实时库，再判定命令成功。北向成功回执等待持久记录及实时投影
均同步完成，避免上级已收到成功而监控仍显示旧状态。
`Pending` 只保证实时库已写入，采集侧登记后才有持久记录；共享内存整体丢失可能丢掉
尚未登记的 Pending。实时库容量耗尽时明确拒绝新提交，当前不自动清理命令记录。
完整部署、命令行参数和故障边界见 `website/docs/processes/collector/commands.md`。

## 构建与安装

需要 CMake 3.21+、C++17 编译器、仓库内 redis-plus-plus、系统安装的 Boost 1.83+ 头文件，
以及与工具链一致的 hiredis（作为 redis-plus-plus 的传递依赖）。Windows MSVC 在
x64 Developer PowerShell 中执行。所有命令均从仓库根目录运行。

共享内存及其 Container/Move/Intrusive 依赖全部来自同一套系统 Boost，不再从仓库
引入单独的 Interprocess 或补充组件。本机使用 `E:/boost_1_89_0`；其他系统由 CMake
查找已安装的 Boost，可通过 `BOOST_ROOT` 或 `CMAKE_PREFIX_PATH` 指定位置。
CMake 优先使用 Boost 配置包，也支持只有头文件的完整 Boost 根目录，不隐式下载：

```powershell
cmake -S backend/tests/rtdb -B build/rtdb-unified-msvc -G "Ninja Multi-Config" `
  -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release `
  -DBOOST_ROOT=E:/boost_1_89_0 `
  -DCMAKE_PREFIX_PATH="$PWD/build/windows-deps-msvc/install" `
  -DCMAKE_INSTALL_PREFIX="$PWD/build/rtdb-unified-msvc/install"
cmake --build build/rtdb-unified-msvc --config Release --parallel 2
ctest --test-dir build/rtdb-unified-msvc -C Release --output-on-failure
cmake --install build/rtdb-unified-msvc --config Release --component EMSRTDB
```

将工具链和依赖前缀改为对应版本可构建 MinGW；不能混用 MSVC 导入库与 MinGW ABI。
Linux 等系统安装 Boost 开发包后可省略 `BOOST_ROOT`，使用系统搜索路径。
当前 C++ API 暴露 STL/Result，调用者必须使用兼容架构、编译器 ABI、配置与 CRT。
只编译共享内存可配置 `EMS_REALTIME_WITH_REDIS=OFF`，不强制安装 Redis 客户端依赖。

安装布局：

```text
bin/                          ems-rtdb 可执行程序及 Windows 运行依赖 DLL
lib/                          导入库或 Linux SO
include/rtdb/                 公共接口，Boost/Redis 实现头不外露
include/common/core/          Result、CallContext 等共享类型
include/common/model/         四遥配置、类型化样本与质量码
lib/cmake/ems_rtdb/            find_package 配置与 ems::rtdb 导出目标
share/ems_rtdb/                配置示例和本文档
```

Windows 将 bin 中的程序与依赖 DLL 一起部署；Linux 还须配置 SO
搜索路径并安装匹配的 hiredis 等运行依赖。调用方无需安装 Boost/Redis SDK，仍需要运行库。
原有业务模型使用的 JSON 头随 SDK 安装，无需调用方单独配置依赖。

常驻入口、DLL 与业务客户端均使用同一份只读部署 JSON。配置文件本身包含后端参数，
但业务代码只传文件路径，具体解析与工厂选择封装在 DLL 内；管理 RPC 自动发现尚未接入。
配置内容应由部署管理发布，不能让各业务进程独立修改。

## 运行与调用

```powershell
# 常驻持有并初始化；Ctrl+C 停止，只关闭本进程资源，不清空共享段。
./build/rtdb-unified-msvc/install/bin/ems-rtdb.exe --config backend/conf/realtime.json
# 初始化/核验后退出；该选项允许创建资源，并非只读配置检查。
./build/rtdb-unified-msvc/install/bin/ems-rtdb.exe --config backend/conf/realtime.json --init-only
```

消费方 CMake：

```cmake
find_package(ems_rtdb 1 CONFIG REQUIRED)
target_link_libraries(my_process PRIVATE ems::rtdb)
```

消费方 C++：

```cpp
#include <rtdb/client/realtime_client.h>

auto ctx = ems::core::make_call_context(std::chrono::seconds(2));
auto opened = ems::rtdb::RealtimeClient::open(config_path, ctx);
if (!ems::core::is_ok(opened)) {
    // 按 ErrorCode 和 WriteEffect 处理失败，不根据错误文本猜后端。
    return;
}
auto client = std::get<0>(std::move(opened));
auto snapshot = client->read(point_key, ctx);
```

`open` 只挂接现有共享段；`initialize` 仅供部署保证唯一性的初始化角色调用。
`create_if_missing` 不允许写入 JSON。普通 close/析构不删除共享资源；
`SharedMemoryStore::remove` 是维护 API，调用方必须先停止并隔离全部使用者。
整组服务由 daemon 关闭时，默认配置的 `cleanupArgs` 会在全部托管进程回收后执行
`ems-rtdb --config conf/realtime.json --cleanup`，删除本部署共享段和命名锁；
不获取遗弃锁，不清空 Redis，不修改 MySQL 配置或点表。独立部署管理者可使用
`RealtimeClient::cleanup` 执行同样的配置驱动清理，但必须先隔离全部使用者。
首批不实现管理者自动选主，唯一常驻实例由部署保证。

共享内存和 Redis 示例分别为 `backend/conf/realtime.json` 与 `realtime-redis.json`。
配置中的 type 由注册工厂识别，未编译/未知后端明确拒绝，不自动换成另一后端。
`credentials_ref` 和管理服务配置属于目标设计，本批解析器尚不支持，会明确拒绝。

## 限制与验证

- Boost 共享段使用 `ems_rtdb_boost_` 名称前缀及布局版本 2，与旧自研段不兼容。
  Windows 的 Boost 默认共享目录需要对应 OS 访问权限，沙箱可能阻止访问。
- 首批使用段级 Boost 命名锁，shard_count 只校验配置兼容。持锁进程崩溃后可持续超时；
  不能强制清锁继续访问，须隔离使用者后维护重建。普通退出不自动清段。
- Redis 目前只支持单个权威主节点。CallContext 在边界检查；第三方同步 I/O 不能硬取消，
  实际等待还受 transport_timeout_ms 与 pool_wait_timeout_ms 限制，可能超过剩余预算。
- 回归覆盖记录并发、来源授权、跨进程命令、统一测点读写、命令顺序投影、反馈回写、
  北向回执补同步及通道状态发布；真实 Redis 用例需配置专用实例，跳过不视为通过。

真实 Redis 联调设置 `EMS_TEST_REDIS_PORT` 为专用测试实例端口后运行 CTest。
测试只清理随机作用域中的已知键，不执行 FLUSHDB。当前结果不代表 Linux/ARM、
真实 Redis、控制授权或完整业务链路已验收。

## 测点接入

```cpp
#include <rtdb/client/point_client.h>

// catalog 由当前设备配置编译；消费者不调用 installWriter。
auto opened = ems::rtdb::PointClient::open(config_path, catalog, ctx);
if (!ems::core::is_ok(opened)) return;
auto client = ems::core::value_of(opened);
auto sample = client->readPointJson("yc", point_id, ctx);
```

只有采集执行者安装 writer，普通测点客户端不能更新数值。采集样本带来源、epoch、
递增序号及当前配置修订，读取统一计算品质和新鲜度。命令目标通过 `CommandClient`
提交，执行者依据持久命令顺序投影 YK/YT，迟到结果不能覆盖后续命令。

`EngineConfig` / `DataDispatcherConfig` 只指定实时库配置文件、站作用域和预算。
通道配置归属于 `net/channel_config.h`，不再依赖已删除的 RTDB 业务模型。
共享内存缺失时业务报错，由 `ems-rtdb` 负责初始化。

旧 business 键不再读写。开发部署需停止全部旧进程，重新初始化实时数据后重新采集；
不要混跑旧客户端与新客户端。MySQL 配置和持久命令记录保留，配置直接修改现有表，
热加载仅递增站根 `config_revision`。
