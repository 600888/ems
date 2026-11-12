# Python 与 C++ 未覆盖功能审计

## 首批修复进度（2026-10-08）

第 1—4 节保留修复前的审计快照；当前状态以本表及 README 为准，第 5 节和声明附录已更新。

| 原编号/问题 | 当前状态 | 修复内容 |
| --- | --- | --- |
| G02 多会话安全隔离 | 已修复 | SessionOptions.security_backend_factory 每会话创建实例；C++ 独占资格检查阻止直接/工厂重复引用，失败不清理其他会话；Client/Server 预校验不再构造临时安全会话。 |
| 安全抽象类的迟到失败及空 reset | 已修复 | Python 构造时检查 8 个必需实现；reset 不再默认空操作；异常报告到 unraisablehook；verify_connect/reset 强制 None 返回值。 |
| G04 低层处理器 | 已修复 | 9 个处理器和 in_executor_thread 全部绑定；参数拥有型复制，None 可移除；REPORT 可由业务决定是否确认。 |
| G05 raw Session 高级服务 | 已修复 | attach_advanced_services 安装现有 C++ AdvancedService，支持可选 TransBridge。 |
| G06 自定义代理 | 已修复 | ProxyProvider 构造器、Python trampoline、async_request 及 ProxyRouter 继承方法；保活、一次完成、错误字段恢复、取消和迟到结果隔离。 |
| G01 真实密码/ESAM | 仍缺具体实现 | 当前只完成后端接口与隔离；真实算法/厂商 SDK 尚未确定，不用 mock 代替。 |
| G03、G07—G14 | 继续保留 | asyncio 专家服务端、同步高级入口、常量/辅助函数、完整记录/地址/对象业务及实际硬件后端尚未补齐。 |

首批增加内存通道、双 TCP 连接及 C++ 回归；完整验证结果见 [验证记录](verification.md)。

审计日期：2026-10-07。范围为当前仓库的 Python 包装层、原生绑定、43 个公开
C++ 头文件及其实现，不把类型声明中的 `...`、异常清理中的 `pass` 或所有
`unsupported_service` 分支直接视为占位实现。

此前“九类缺口补齐”表示那九类 API 已接入 C++，不表示真实安全后端、设备业务或
全部 C++ 公共入口已经实现。尤其真实 ESAM 是**缺少具体后端实现**，不能只标为
“等待硬件验收”。

## 1. Python 空方法：完整清单

对 `python/src` 的所有 `.py` 函数做 AST 检查，找到以下 9 处。

| 位置 | 方法 | 当前行为 | 判定 |
| --- | --- | --- | --- |
| [security/backend.py](src/dlt698/security/backend.py#L13) | `begin_connect` | `raise NotImplementedError` | 无默认认证材料生成 |
| [security/backend.py](src/dlt698/security/backend.py#L18) | `accept_connect` | 同上 | 无默认服务端认证 |
| [security/backend.py](src/dlt698/security/backend.py#L21) | `verify_connect` | 同上 | 无默认客户机认证结果验证 |
| [security/backend.py](src/dlt698/security/backend.py#L24) | `protect_request` | 同上 | 无默认请求保护 |
| [security/backend.py](src/dlt698/security/backend.py#L27) | `open_request` | 同上 | 无默认请求验签、验证或解密 |
| [security/backend.py](src/dlt698/security/backend.py#L30) | `protect_response` | 同上 | 无默认响应保护 |
| [security/backend.py](src/dlt698/security/backend.py#L33) | `open_response` | 同上 | 无默认响应验签、验证或解密 |
| [security/backend.py](src/dlt698/security/backend.py#L36) | `reset` | 只有文档字符串，返回 `None` | 空清理，不会清除应用后端状态 |
| [app/_lifecycle.py](src/dlt698/app/_lifecycle.py#L35) | `Lifecycle.close` | `raise NotImplementedError` | 内部基类扩展点；Client、Server 已分别覆盖，不是用户入口缺失 |

前 8 项对应 C++ [IBackend](../cpp/include/dlt698/security/backend.hpp#L14) 的 8 个纯虚函数。
C++ [backend.cpp](../cpp/src/security/backend.cpp) 也只有基类构造和析构，没有具体密码后端。
Python [trampoline](bindings/security/backend.cpp) 已完成委托、拥有型参数复制、GIL 和错误转换；
它不会自动提供密码学实现。`SecurityBackend` 目前允许直接实例化，也没有在配置时检查
子类是否完整覆盖所有必需方法，遗漏方法会在协议调用时失败。

[安全测试](tests/security/test_security.py#L10) 使用固定 `a/b/c/d` 和原样返回的应用字节，
验证的是委托及状态机，不是密码认证、MAC、签名、加解密、密钥管理或真实 ESAM。

## 2. 已确认的功能及入口缺口

| 编号 | 未覆盖项 | 当前范围、替代入口与源码证据 |
| --- | --- | --- |
| G01 | 具体安全后端 | 没有真实密码认证、对称认证、签名认证及 ESAM/厂商 SDK 适配器；密钥、随机数/会话新鲜度、防重放和清理策略依赖应用后端。[IBackend](../cpp/include/dlt698/security/backend.hpp#L14)、[Python 基类](src/dlt698/security/backend.py) |
| G02 | 多连接安全后端实例隔离 | `Engine.attach` 给每个 Session 复制同一份 `options_`，其中 backend 是同一个 `shared_ptr`；C++ app::Server 也复制同一份 protocol。没有 backend 工厂或禁止多会话复用的检查，与 IBackend“同一实例不得在多个 Session 中复用”的契约冲突。一个会话 reset 可能影响另一会话的认证材料。优先修复。[Engine.attach](bindings/session/engine.cpp#L81)、[Server 接入](../cpp/src/app/server.cpp#L66) |
| G03 | asyncio 专家服务端 | 高层 `AsyncServer` 只包装托管 Server/Device，没有 `ObjectRegistry`、`AdvancedServiceOptions`、`TransBridge` 或逐连接 Session 的集成入口。动态 provider、SET/ACTION/记录和高级服务可以使用 Engine/Endpoint，但尚无统一 asyncio 服务端封装。[AsyncServer](src/dlt698/app/async_server.py#L27)、[Endpoint](src/dlt698/expert/endpoint.py#L19) |
| G04 | 低层 Session 的处理器绑定 | 缺少 `set_request_handler`、`set_record_handler`、`set_set_handler`、`set_action_handler`、`set_advanced_handler`、`set_diagnostic_handler`、`set_report_handler`、`set_follow_handler`、`set_acd_handler`，以及 `in_executor_thread` 查询。普通对象服务可用 `attach_services`；Engine 已转换诊断、REPORT、FollowReport、ACD 为队列事件，但 raw Session 无相应扩展入口。[C++ Session](../cpp/include/dlt698/session/session.hpp#L86)、[当前绑定](bindings/transport/channels.cpp#L308) |
| G05 | raw Session 安装高级服务 | `AdvancedService` 没有公开 Python 构造器，也没有 `attach_advanced_services`；`attach_services` 只安装 ServerService。Engine 内部已安装 AdvancedService，因此不能把 MD5/ThenGet/PROXY 全部判为未实现。[attach_services](bindings/transport/channels.cpp#L332)、[Engine 安装](bindings/session/engine.cpp#L137) |
| G06 | 自定义代理提供者 | Python `ProxyProvider` 没有构造绑定、trampoline 或 `async_request` 方法，不能像 ObjectProvider/SecurityBackend 那样自定义目标访问；`ProxyRouter.async_request` 也未直接绑定。已实现的 `ProxyRouter.bind` 可将代理路由到原生 Session。[绑定](bindings/service/providers.cpp#L160)、[C++ 合约](../cpp/include/dlt698/service/advanced.hpp#L10) |
| G07 | 同步高层高级事务入口 | `Client`/`NativeClient` 没有 `exchange` 或 MD5、ThenGet、PROXY 专用方法；这在 C++ app::Client 也没有。异步侧可用 `AsyncClient.exchange`，显式驱动侧可用 Engine/SessionHandle.async_exchange。`ClientService`、`SyncClientService` 也未作为独立 Python 类公开，但其普通读写/记录能力已有替代入口。[同步 Client](src/dlt698/app/client.py)、[异步 exchange](src/dlt698/app/async_client.py#L325) |
| G08 | 标准 OI 符号常量 | C++ `standard::oi` 的 132 个公开常量均未导出为 Python `standard.oi`，Python 需使用数值 OI 或目录查询。132 是常量声明数，不等于互不重复的对象数。[oi.hpp](../cpp/include/dlt698/standard/oi.hpp)、[Python standard](src/dlt698/standard/__init__.py) |
| G09 | 独立辅助函数 | `from_hex`、`to_hex`、`md5`、`valid_time_tag` 未绑定。十六进制与 MD5 可用 Python 标准库，但 C++ 日历 TimeTag 校验没有独立 Python 入口；会话内部仍执行该校验。Reader/Writer 型增量读写函数和分服务 encode/decode 也未逐一导出，完整 codec 已公开。[bytes.hpp](../cpp/include/dlt698/common/bytes.hpp)、[time_tag.hpp](../cpp/include/dlt698/protocol/apdu/time_tag.hpp)、[codec 绑定](bindings/codec/codec.cpp) |
| G10 | 内置记录后端完整筛选业务 | `MemoryRecords` 仅支持 RSD 0/1/2/9；未实现 3/4/5/6/7/8/10、非 NULL 采样间隔、MS 表计集合筛选、数据库/事件业务。对应模型和线上 codec 已实现，应用自定义 provider 可以解释选择条件。[MemoryRecords 合约](../cpp/include/dlt698/service/memory_records.hpp#L43)、[筛选实现](../cpp/src/service/memory_records.cpp#L148) |
| G11 | 标准记录的 ROAD 和扩展模板 | 标准校验拒绝 ROAD 列和嵌套记录列；仅有日冻结、月冻结、掉电、初始化 4 类模板；本批事件发生源仅接受 NULL，未覆盖其他事件的源类型和完整专用字段。通用 ROAD/RCSD 模型与 codec 已实现。[records.cpp](../cpp/src/standard/records.cpp#L8) |
| G12 | 非单地址会话及地址转换 | 帧 codec 保留组、通配和广播地址；Session 构造器只接受 single，接收仅做精确 SA/CA 匹配。组/通配业务匹配、广播无需应答的会话行为、BCD 地址文本转换未实现。[Session 构造](../cpp/src/session/session.cpp#L1606)、[地址匹配](../cpp/src/session/session.cpp#L1105)、[覆盖记录](../docs/protocol-coverage.md) |
| G13 | 完整标准对象/特征/单位语义 | 标准目录只收录本批常用属性，不是完整接口类及可选方法目录；标准校验和 MemoryObject 不处理非零特征的快照业务；单位名称只包含本批使用的单位。未收录对象仍可通过自定义 ObjectSchema/provider 处理。[catalog.hpp](../cpp/include/dlt698/standard/catalog.hpp#L58)、[特征拒绝](../cpp/src/standard/catalog.cpp#L811)、[unit_symbol](../cpp/src/standard/catalog.cpp#L1023) |
| G14 | 内置设备业务和硬件适配器 | Device 是只读数据发布入口，没有默认远端写入、方法执行和记录业务；MemoryRecords 不自动产生冻结/检测事件。实际计量、开关控制、采集任务、持久化数据库、真实透明端口、ESAM SDK 及特定 RS-485 方向/排空驱动未内置，必须提供应用后端。通用对象 provider、路由和 TransBridge 已实现。[Device](../cpp/include/dlt698/service/device.hpp#L21)、[MemoryRecords](../cpp/include/dlt698/service/memory_records.hpp#L19)、[透明桥](bindings/transport/trans.hpp) |

G01/G10/G11/G12/G13/G14 包含 C++ 本身尚未覆盖的业务或协议语义，不能仅靠补 Python 绑定解决。
G03 至 G09 主要是 Python 公共入口不足，其中部分有现成低层替代路径。

## 3. 已实现但仍有入口限制

以下不是空方法，仍需与“完整支持”区分。

- **托管对象拒绝 Python hooks**：Client、Server、AsyncServer 的原生工作线程不调用
  Python 安全/日历/串口方向或排空回调。对应功能目前只能在调用线程驱动的
  Engine、AsyncClient、低层 Session/SerialLinkChannel 上使用；没有提供可直接配置到
  托管 Python API 的具体原生安全后端/原生硬件 hook 对象。
  [构造检查](bindings/app/app.cpp#L106)、[串口检查](bindings/app/async_server.cpp#L61)。
- **1.5 停止位**：高层 C++ Client/Server 和 Python Engine 的配置串口入口明确拒绝。
  raw SerialChannel 会把 one_point_five 交给系统驱动，因此不是所有串口入口都没有实现。
  [Engine 检查](bindings/session/engine.cpp#L203)、[raw 串口配置](../cpp/src/transport/serial.cpp#L133)。
- **AsyncClient 串口打开阶段**：`Engine.open_serial` 在调用线程同步打开和配置端口，
  之后才异步等待 LINK/CONNECT；没有把可能阻塞的打开阶段交给 C++ 工作线程。
  [open_serial](bindings/session/engine.cpp#L192)。
- **Engine 监听通道预算**：listen 没有 ChannelOptions 参数，TcpListener 使用默认通道预算；
  拨号可传入预算，托管 ServerOptions.channel 也可配置。
  [Engine.listen](bindings/session/engine.cpp#L261)。
- **REPORT 业务确认决策**：Engine 成功入队即返回 true 让 C++ 确认，Python on_event
  无法按业务处理结果选择是否确认；raw Session 的 bool ReportHandler 尚未绑定。
  [REPORT 队列](bindings/session/engine.cpp#L114)。
- **底层独立类/方法未全部公开**：IoRuntime.run 未绑定，可用 run_for；ServerService
  可由 attach_services 替代；TrafficEvent/Direction 可由拥有型 Event 替代；C++ 模板、
  variant、ByteView、Result 多数转换为 Python Data、bytes、联合类型及异常，不能按
  声明数量认定为功能缺失。

## 4. 其他抽象扩展点和默认拒绝路径

这些接口也需要应用实现，但已有对应原生实现可用，不应误报成 SDK 全部未实现：

| 扩展点 | 必须提供/默认行为 | 已有实现 |
| --- | --- | --- |
| `ObjectProvider` | `read` 必须覆盖；默认 write/invoke/read_record 返回拒绝 DAR=3 | MemoryObject、MemoryRecords；业务方法需显式 bind/register |
| `IExecutor` | 自定义子类必须实现 post/schedule/now/is_current | ManualExecutor、IoRuntime.executor |
| `ITimer` | 自定义子类必须实现 cancel | 原生执行器返回原生 timer |
| `IChannel` | 自定义子类必须实现 async_read/async_write/close | MemoryChannel、TcpChannel、SerialChannel、SerialLinkChannel |
| `ProxyProvider` | C++ async_request 为纯虚；Python 尚无子类桥接 | 原生 ProxyRouter |

未协商能力、未知服务/标签、地址或 TimeTag 错配、未注册对象以及权限拒绝，属于已实现
的协议错误处理。`_native.pyi` 中的 `...` 是类型声明；`ProxyProvider` 的 `pass` 是
无公开成员的声明；AsyncClient 等待状态循环的 `pass` 也不是空业务方法。
ThenGet、REPORT、PROXY、FollowReport、ACD 和两类分段已有实现，旧计划中的未勾选项及
部分覆盖网页已经过时，不能直接据此判定未实现。

## 5. 完整未直接映射声明附录与核对结果

- [未直接映射的全部 C++ 声明](unbound-api-audit.md)：301 条，逐条给出原始签名、头文件
  和分类说明，包括常量、真正未绑定的方法、已有替代入口的类和语言适配声明。
- AST 清单核对通过：43 个公开头文件，1300 条公开声明；其中清单标记 exposed 885 条、
  deferred 301 条、internal 114 条。`deferred` 只是没有直接映射，不能当成 301 个功能缺口。
- API/runtime 核对通过：925 条人工契约记录。该检查核对绑定存在性，不验证密码和设备业务。
- 实际安装扩展确认 9 个 Session 处理器、执行器线程查询、高级服务安装和自定义代理入口；
  SecurityBackend 直接实例化或遗漏必需方法在构造时失败，reset 错误向 unraisablehook 报告。
- 实际 wheel 完整 Python 回归 145 项、C++ CTest 11 组通过；安全相关用例验证 mock 委托、
  独占生命周期和多连接隔离，仍不证明真实密码或 ESAM。

真实串口/RS-485、真实 ESAM、外部设备互操作、其他平台/解释器和 sanitizer/fuzz 等缺少
验收证据，应列为未验证，与缺少实现分开。真实安全后端仍缺具体实现；首批修复没有将
抽象安全接口填成假认证成功，也没有创建 Python 工作线程。
