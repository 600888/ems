# dlt698

从零构建的现代 C++17 DL/T 698.45 协议库，面向集中器、采集终端和电力网关等协议对接场景。

**在线接口文档：<https://600888.github.io/dlt698/>**

实现基线：DL/T 698.45—2017。

Python 版本位于同级 `python/`，直接绑定相同 C++ 内核并共用根 `VERSION`。同步/异步读取、写入、记录和专家扩展的安装与使用见 [Python 文档](python/README.md)，本地验证和外部验收范围见 [验证记录](python/verification.md)。C++ 默认构建保持独立。

## 启动服务端

```cpp
#include <dlt698/app.hpp>

dlt698::app::Server server;
auto value = server.set({0x200F, 2, 0}, dlt698::model::UInt16{5000});
auto started = server.start_tcp("0.0.0.0", 6980);
```

检查 `value` 和 `started` 后，应用可以继续自己的业务，运行中仍可调用 `server.set(...)`。频率 `5000` 表示 `50.00 Hz`；库自动运行 I/O、接入连接、登录和应答。串口使用 `server.start_serial("COM3", 9600)`。结束时调用 `server.stop()`，析构也会收尾，设备数据跨连接和同实例重启保留。

链接 `dlt698::dlt698`，开启 `DLT698_BUILD_TRANSPORT`。完整错误处理、地址/布局配置与高级入口见[托管服务器](website/docs/session/server.md)，可直接运行新示例 `dlt698_server`。

## 连接客户端

普通客户端同样只需创建、连接、读取：

```cpp
dlt698::app::Client client;
auto connected = client.connect_tcp("127.0.0.1", 6980);
// 检查 connected 成功后再读取；value 保留 Data/DAR。
auto value = client.get({0x200F, 2, 0});
```

运行 `dlt698_server` 后在另一终端运行 `dlt698_client`，批量读取频率、三相电压/电流、
有功/无功功率、功率因数、分费率电能和通信地址，并演示单独读取 A 相电压及费率 1 电能后断开。串口使用
`client.open_serial("COM4", 9600)`。CONNECT、运行线程和分块读取由库管理，支持直接
`get/set/action` 及列表/记录；完整返回结果、超时和退出约定见[托管客户端](website/docs/session/client.md)。

## 能力一览

### APDU 服务

| 服务 | 编解码 | 会话 / 服务层 | 备注 |
| --- | :---: | :---: | --- |
| LINK | ✅ | ✅ | 自动周期心跳、TimeTag |
| CONNECT | ✅ | ✅ | 公共认证及可注入 ESAM 认证后端 |
| RELEASE | ✅ | ✅ | 含 Notification |
| GET Normal / NormalList | ✅ | ✅ | 逐项 Data / DAR |
| GET Record / RecordList | ✅ | ✅ | 完整记录选择器 |
| GET Next | ✅ | ✅ | 按行拆分收齐 |
| SET Normal / NormalList | ✅ | ✅ | 精确 OAD 匹配 |
| ACTION 普通 / 列表 | ✅ | ✅ | 完整 OMD 模式匹配 |
| ERROR | ✅ | ✅ | 远端原码与 TimeTag 回显 |
| REPORT | ✅ | ✅ | 三类通知/确认、有限重发 |
| PROXY | ✅ | ✅ | 七类代理、路由及端口 provider |
| SECURITY | ✅ | ✅ | 外层 codec / 后端状态机；实机 ESAM 待接入 |
| FollowReport / ACD | ✅ | ✅ | 跟随结果及业务观察回调 |
| ThenGetNormalList | ✅ | ✅ | 依次执行、单调延时、独立读取结果 |
| MD5 | ✅ | ✅ | 完整 Data 编码的一致性摘要 |

高级服务用法见 [REPORT / ThenGet / PROXY 接入](docs/advanced-services.md)，安全后端和联调材料见 [ESAM 接入](docs/esam-integration.md)。

### 编解码与链路

| 能力 | 状态 |
| --- | :---: |
| A-XDR 长度（短 / 长形式） | ✅ |
| 单帧长度、控制域、HCS / FCS | ✅ |
| 扰码 SC（单帧与分帧头/片段 ±33H） | ✅ |
| 流解析（半帧、粘帧、噪声、校验失败恢复） | ✅ |
| 链路分帧与逐帧确认（12 位回绕、有界重组、有限重发） | ✅ |
| 变长 SA / 逻辑地址 / CA | ✅ |
| OI / OAD / OMD / TI / TSA | ✅ |
| ROAD / Region / RSD / MS / CSD / RCSD | ✅ |
| Data 标签 0–96（40 类） | ✅ |
| Data 标签 MAC / RN / SID / SID_MAC / COMDCB | ✅ |
| 广播地址的会话行为（无需应答） | ❌ |

### 传输与运行时

| 能力 | 状态 |
| --- | :---: |
| TCP 客户端 / 监听 / 双向拨号 | ✅ |
| 原始串口（速率、字格式、流控、串行队列） | ✅ |
| 串行时序适配（FE 前导、33 位间隔、RS-485 方向 hooks） | ✅ |
| 内存通道 MemoryChannel | ✅ |
| 异步执行器 ManualExecutor / IoRuntime | ✅ |
| 同步服务 SyncClientService | ✅ |
| 单在途事务、PIID / OAD / OMD 匹配与隔离 | ✅ |
| 自动重拨号 / 自动重登录 | ❌ 由应用负责 |
| 真实串口与 RS-485 硬件时序 | ⚠️ 未验证 |

### 对象与数据

| 能力 | 状态 |
| --- | :---: |
| ObjectRegistry / MemoryObject 读写与 schema | ✅ |
| ClientService / ServerService 方法分发 | ✅ |
| 122 个常用及安全/端口 OI（电能、需量、状态、谐波、参数） | ✅ |
| 记录模板 4 个入口 / 5 个记录列，共 131 个 OI | ✅ |
| 记录选择器 RSD 0–10、MS 0–7、CSD、Region 0–3 | ✅ |
| 有界行列筛选、不可变快照、能力筛选、点位探测 | ✅ |
| 非公共认证机制、SECURITY 封装 | ✅ 后端接口；真实 ESAM SDK 待接入 |

完整矩阵与验证证据见 [支持矩阵与验证范围](docs/protocol-coverage.md)。

## 构建与测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix ./build/stage
```

| 选项 | 说明 |
| --- | --- |
| `-DDLT698_BUILD_TRANSPORT=OFF` | 关闭 Asio TCP/串口；内存通道、SerialLinkChannel、会话、同步/异步服务仍可用 |
| `-DBUILD_SHARED_LIBS=ON` | 构建单一共享库（默认 ON）；设为 OFF 可构建单一静态库 |
| `-DDLT698_WARNINGS_AS_ERRORS=ON` | 严格警告检查 |

也支持 `cmake -S cpp -B build/core` 使用独立 C++ 构建入口。需要 C++17、CMake 3.20+ 和匹配的 C++ 编译器，不依赖 Python。所有构建树都在 `build/` 下，`build/core` 使用独立入口、`build/shared` 编共享库，CI 默认用 `build/`。

### 单元测试

测试基于 Catch2（`third/Catch2`的 amalgamated 发行版），按被测层次分子目录，每个目录生成一个可执行文件：

| 目录 | CTest 名称 | 覆盖内容 |
| --- | --- | --- |
| `cpp/tests/common` | `common` | Result/Error、字节视图与读写游标、ManualExecutor 虚拟时钟 |
| `cpp/tests/codec` | `codec` | Data 编解码与资源限制、RSD/RCSD/MS 记录描述符 |
| `cpp/tests/protocol/link` | `protocol_link` | 帧编解码、CRC、流式拆帧、分片与重组 |
| `cpp/tests/protocol/apdu` | `protocol_apdu` | 统一 APDU 路由、连接服务、GET 家族、SET/ACTION、分块、时间标签 |
| `cpp/tests/service` | `service` | 对象目录与 provider、标准对象与记录、客户端/服务器服务、同步封装 |
| `cpp/tests/service` | `device` | 标准数据发布、自定义定义、预算及并发快照 |
| `cpp/tests/session` | `session` | 状态机、事务匹配、取消与超时、诊断回调 |
| `cpp/tests/transport` | `transport` | 内存通道、串行链路适配 |
| `cpp/tests/transport` | `transport_tcp` | TCP 通道与 IO 运行时 |
| `cpp/tests/app` | `app` | 托管两端、多连接、读写/记录、登录、超时、取消与回调析构 |

公用辅助在 `cpp/tests/catch/test_support.hpp`：`hex()`、`fixture()`、`require_ok()`、
`require_error()`、`require_truncation_rejected()` 与 `CHECK_DECODE_ERROR` 宏。
协议预期字节取自 `tests/vectors/` 的独立规范向量，不由被测编码器生成。

筛选运行：

```sh
ctest --test-dir build -R codec --output-on-failure        # 按测试名
./build/bin/dlt698_test_protocol_apdu "[apdu][get]"        # 按 Catch2 标签
```

`transport_tcp` 和 `app` 默认参与 CTest，带 `network` 标签，需要真实回环 socket。
传输实现统一启用 Asio 线程支持，已修复 MinGW 因头文件包含顺序不同而混用
有线程/无线程类型的静态链接崩溃。可用 `ctest --test-dir build -L network --output-on-failure`
单独执行网络回归；受限环境不能联网时应记录未执行范围。

## 集成

### 预编译发布包

[GitHub Releases](https://github.com/600888/dlt698/releases) 按平台、架构和库类型发布 C++ SDK 压缩包，共 10 个包；每个包包含 `include/` 公开头文件目录和对应的静态库或动态库。Python wheel 和 sdist 通过独立任务发布到 PyPI。C++ 消费方可以直接下载对应包，也可按 [本地打包命令](docs/releasing.md#本地打包) 自行生成：

| 平台 | 架构 / 工具链 | 产物 |
| --- | --- | --- |
| Linux | x64、ARM64 / GCC | 静态 `.a`、动态 `.so`，分别打包为 `.tar.gz` |
| Windows | x64 / MSVC、Release `/MD` | 静态 `.lib`、动态 `.dll` + 导入 `.lib`，分别打包为 `.zip` |
| macOS | Intel x64、Apple Silicon ARM64 / AppleClang | 静态 `.a`、动态 `.dylib`，分别打包为 `.tar.gz` |

文件名例如 `dlt698-1.0.0-windows-x64-msvc-shared.zip`。每个包均含一个 `dlt698` 库、公开头文件（包括生成的导出头）、CMake 配置、项目与 Asio 许可证、`VERSION` 和 README；不含测试和示例程序。静态与动态包各有独立安装前缀，不能混合覆盖。Unix 解压请保留共享库符号链接。

解压后将包根目录加入 `CMAKE_PREFIX_PATH`，再使用下面的 `find_package` / `target_link_libraries`。Windows 需兼容的 MSVC 工具链和运行库，动态包的 `bin/` 中 DLL 需放在程序旁边或加入 `PATH`。Linux 需兼容构建环境的 glibc、libstdc++；各系统的 C++ ABI 不保证跨工具链兼容。Release 提供每个压缩包的独立 `.sha256`、发布清单及总校验文件 `SHA256SUMS`，本地打包也生成独立 `.sha256`。

发布流程与本地打包命令见 [Release CI](docs/releasing.md)。

```cmake
find_package(dlt698 1.0 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE dlt698::dlt698)
```

| CMake 目标 | 当前内容 |
| --- | --- |
| `dlt698::dlt698` | 单一库，包含编解码、内存通道、会话、对象服务，以及启用时的 TCP/串口和托管 Server/Client |

旧组件名称 `dlt698::core`、`dlt698::session`、`dlt698::service` 保留为统一库的兼容别名；启用传输时也提供 `dlt698::transport` 和 `dlt698::app` 别名，均不生成独立库。

默认动态构建在 Windows 生成 `dlt698.dll` 和导入库 `dlt698.lib`，Linux/macOS 生成 `libdlt698.so` / `libdlt698.dylib`。Windows 共享库运行时需将安装目录的 DLL 放在程序旁边或加入 PATH。

## 示例程序

示例位于构建目录 `bin/`（Visual Studio 多配置构建为 `bin/Release/`），普通入口在 `cpp/examples/app`，其余源码按用途分三个目录：
`cpp/examples/codec`（协议编解码）、`cpp/examples/service`（对象服务，全部跑在内存通道上）、
`cpp/examples/transport`（真实 TCP 与串口）。

| 普通程序 | 用途 |
| --- | --- |
| `dlt698_server` | 发布 8 组模拟电表属性后启动 TCP/串口，按 Enter 停止 |
| `dlt698_client` | 批量读取模拟属性及单相/费率元素，显示实际值和单位后断开 |

`app/` 和 `transport/` 示例在源码中显式配置电表地址，默认 `000000000000`
（六个 `0x00` 字节），逻辑地址和客户端地址 CA 均为 0。修改时两端配置须一致；
地址按线序填写，低有效字节在前，例如 `123456789012` 对应 `{0x12, 0x90, 0x78, 0x56, 0x34, 0x12}`。
托管服务端的通信地址属性 `4001/2/0` 复用同一配置发布。

### 编解码层

| 程序 | 说明 |
| --- | --- |
| `dlt698_decode` | 十六进制帧解码，可用固定测试帧验证 |

### 对象服务层（无需设备）

| 程序 | 说明 |
| --- | --- |
| `dlt698_memory_get` | 内存通道 CONNECT → GET NormalList（部分成功）→ RELEASE |
| `dlt698_memory_mutation` | 同步 CONNECT → SET → ACTION → GET → RELEASE，输出 `SET DAR=0 ACTION DAR=0 GET UInt16=42` |
| `dlt698_standard_points` | 常用固定 OI 的内存通信示例 |
| `dlt698_standard_points_extended` | 分相电能、带发生时间的需量、谐波、状态字与参数 |
| `dlt698_standard_records` | 记录模板、有界行列筛选、GET Next 按行收齐 |

### 传输层（真实 socket / 串口）

服务端与客户端各一个独立程序，模拟数据相同，可直接配对运行：

| 程序 | 角色 | 说明 |
| --- | --- | --- |
| `dlt698_tcp_server` | 协议服务器 | `TcpListener` 监听，循环接受客户机，LINK 登录 + 心跳 |
| `dlt698_tcp_client` | 协议客户机 | 主动拨号，CONNECT → 命令 → RELEASE 后退出 |
| `dlt698_rtu_server` | 协议服务器 | 打开串口 + `SerialLinkChannel`，应答请求直至时限 |
| `dlt698_rtu_client` | 协议客户机 | 打开串口 + `SerialLinkChannel`，执行一次命令 |
| `dlt698_master` / `dlt698_terminal` | 双角色 | TCP 双向拨号，用于验证协议角色与拨号方向相互独立 |

```sh
dlt698_decode "68 17 00 43 05 07 09 19 05 16 20 00 15 60 05 01 01 40 01 02 00 00 C6 07 16"

# TCP：先起服务端（可省略 lifetime，默认 60 秒）
dlt698_tcp_server 0.0.0.0 6980 60
dlt698_tcp_client 127.0.0.1 6980 get        # 也可 set 25 / action 25 / record

# RTU：两端各占一个真实串口
dlt698_rtu_server COM3 9600 60
dlt698_rtu_client COM4 9600 get
```

源码：[transport/](cpp/examples/transport)、[service/](cpp/examples/service)、[codec/](cpp/examples/codec)。完整用法与资源预算见 [使用说明](docs/m4-m5.md)、[C++ API 摘要](docs/cpp-api.md)、[标准测试向量](tests/vectors/README.md)。

## 已知限制

- 实际 ESAM 认证/MAC/加解密交给厂商 SDK 后端；缺少模块型号、SDK 和测试凭据，实机安全验收待完成。
- 记录选择器的采集与数据库语义由应用 provider 实现。
- 原始串口与 RS-485 硬件时序需实测验证；手动方向切换必须提供真实排空驱动。
- 型号专用对象与端口转发须配置 provider；新增高级服务目前使用分层 Session API。
