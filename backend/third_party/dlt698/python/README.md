# dlt698 Python

Python 包直接绑定同一提交的 C++17 内核，独立构建不改变 C++ 安装包。版本来自根 `VERSION`，导入时核对安装 metadata、包装层和原生扩展的构建身份。

开发环境需 CPython 3.11–3.14、C++17 编译器及 CMake。Windows 使用 x64 MSVC Release `/MD`。

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -e ".[dev]"
.venv/Scripts/python -m pytest python/tests
```

通过 `python -m pip install dlt698` 从 PyPI 安装，或从 Release 工作流的 Actions artifacts 下载对应解释器和平台的 wheel 后，使用 `python -m pip install 文件名.whl`。源码开发也支持上面的 editable 安装；构建后端固定为 scikit-build-core 0.11.6，pybind11 使用仓库内 3.1.0。包没有运行时第三方 Python 依赖。源码包自带内核与需要的第三方头文件，无 Git 也可重建。

```python
from dlt698 import Client, Data, Oad, Server

frequency = Oad(oi=0x200F, attribute=2)
with Server() as server:
    server.set(frequency, Data.uint16(5000))
    server.start_tcp("127.0.0.1", 0)
    with Client() as client:
        client.connect_tcp("127.0.0.1", server.local_port)
        result = client.get(frequency)
        print(result.require_data().as_uint16())  # 原始值 5000，单位/倍率查标准目录
```

## 接口层次与阶段

| 入口 | 已实现能力 | 约定 |
| --- | --- | --- |
| `dlt698.Client / Server` | TCP、简单/完整参数串口、普通/列表 GET、SET、ACTION、记录查询、共享 Device、取消与关闭 | 复用 `app` 的线程、关联和分块；一个在途请求，冲突为 busy；不重连或重试副作用 |
| `dlt698.model` | 全部现有 Data 标签、精确标量、OAD/OMD、RSD 0–10、MS 0–7、ROAD/RCSD、安全描述符 | 标签保留；整数拒绝 bool/溢出，字节复制；日期保留原始字段 |
| `dlt698.codec / protocol` | Data/帧/APDU/SECURITY、流解析、GET/SET/ACTION/记录/ThenGet/MD5/REPORT/七类 PROXY 消息 | 复用原生编码与资源限制，完整字段及逐项 DAR 无损保留 |
| `dlt698.standard` | 标准对象/属性查询、相别/费率/电能/需量/谐波 OAD、工程量、记录查询模板 | 原始值可转换为精确十进制文本；浮点转换需显式选择 |
| `dlt698.expert` | 显式驱动 Engine/Endpoint、provider/schema、MemoryRecords、REPORT/Follow/ACD、ProxyRouter、TransBridge、安全后端 | 由同一业务线程持续 `poll()`；Python 扩展点必须及时返回，不能重入或阻塞等待同一运行时 |
| `dlt698.AsyncClient` | asyncio TCP 关联、GET/SET/ACTION/记录及高级交换、取消、失败重试与异步上下文 | 直接驱动原生异步 Session；loop 所属线程使用，loop 关闭前 `aclose()` |
| `dlt698.AsyncServer` | asyncio TCP/串口启停、多连接、共享 Device、事件自动分发、取消收尾及重新启动 | 复用 C++ `app::Server`；启停等待线程也由 C++ 管理，Python 不创建工作线程 |

普通入口优先；完整消息和扩展点放在分层模块。公开导出都使用显式 `__all__`，扩展实现模块 `_native` 为内部接口。低层自定义 channel/executor、手动方向及硬件 drain hooks、自动能力探测、异步客户端串口和其他 ABI 当前登记为待开放，详见 `api-map.json`；这不会建立另一条版本线。

源码按 C++ 业务层次组织，原生绑定也使用对应目录：

```text
python/src/dlt698/
  app/        Client、Server、AsyncClient、AsyncServer、生命周期
  common/     错误、资源限制、执行器与计时器
  codec/      Data、链路帧及 APDU 编解码
  model/      原生协议值
  protocol/   apdu 消息与 link 帧接口
  security/   安全后端扩展点
  service/    Device、对象目录、provider、记录、代理
  session/    Engine、会话状态、完成通知
  standard/   标准对象、工程量、记录校验、能力与读取规划
  transport/  原生/自定义通道、I/O 运行时、串口与 RS-485、透明转发桥
  expert/     Endpoint 与原有专家入口的兼容汇总
```

推荐 `from dlt698.app import AsyncServer`、`from dlt698.service import ObjectRegistry` 等
业务导入；根入口及原有 `dlt698.client / server / async_client / errors / expert` 导入仍可使用。
生成工具写入新的目录，不会重新产生扁平模块。
`python/tests/` 同样按 app/common/model/protocol/security/service/session/standard/transport/release/interop
分目录；`tools/` 保留构建、生成和发布工具，`examples/` 保留独立示例。

```python
import asyncio
from dlt698 import AsyncClient, AsyncServer, Data, Oad

async def main():
    attribute = Oad(oi=0x200F, attribute=2)
    async with AsyncServer() as server:
        server.set(attribute, Data.uint16(5000))
        await server.start_tcp("127.0.0.1", 0)
        async with AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            print((await client.get(attribute)).require_data().as_uint16())

asyncio.run(main())
```

`AsyncServer` 的网络与会话线程来自 C++ `app::Server`，绑定层的 `ServerRunner` 用
C++ 线程执行启停等待，通过完成通知唤醒 asyncio Future。Python 不使用
`Thread`、线程池、`to_thread` 或 `run_in_executor`。`on_event` 在 loop 所属线程自动分发，
异常交给 loop 的异常处理器；回调内可调用 `close()`，之后不再分发事件。
`await stop()` 保留设备并允许重启；`close()` 发起终止，`await aclose()` 等待完整收尾。
启动取消时会等原生操作结束再停止，避免取消后迟到的监听器继续运行。

九类已确认缺口的实现入口、回调约定及验收范围见 [补全记录](feature-gaps.md)。

`AsyncClient` 同时提供 `await open_serial(path, baud=9600)` 和
`await open_serial_configured(path, serial, link, profile)`。本地端口打开/配置为原生操作，
后续 I/O、LINK 和 CONNECT 通过原生完成队列等待；失败会排空旧运行时，允许重新连接。
`await disconnect()` 等待 RELEASE 后关闭通道，保留客户端供重连；`close()` 强制终止，
`await aclose()` 在空闲关联时优先释放协议连接，再确定性收尾。

```python
from dlt698.service import ProbeOptions
from dlt698.standard import ReadService, service_support

# client 已通过 TCP 或串口关联；预设连接使用本地能力，公共连接使用 CONNECT 协商快照。
support = service_support(client.capabilities, ReadService.list)
points = await client.probe_points(attributes, ProbeOptions(batch_size=16))
for point in points:
    print(point.attribute, point.outcome, point.validation_error)
```

探测复用 C++ 读取规划及逐项校验：保留重复点与顺序，不按业务提示自动删除候选。
`PointResult.outcome` 为 Data、原始 DAR 或 Error，标准校验失败保留 Data 和 validation_error。
离线规划与验证通过 `standard.plan_reads / candidate_points / validate_record_*` 使用。

## 数据、配置和错误

`ReadResult` 只有 Data 或 DAR 之一。`require_data()` 是显式要求业务成功，非零 DAR 抛 `DarError`。列表和 SET/ACTION 保留逐项结果；应用必须自行检查 DAR，不自动回滚或重试。

本地失败抛 `Dlt698Error` 的分类子类，保留 `code / offset / context / remote_code`。`context_bytes` 保存原始文本字节：核心文本为 UTF-8，Windows 系统 I/O 文本可能来自系统代码页；显示文本使用适当解码。超时仍是 `TimeoutError` 的子类，异步取消使用 `asyncio.CancelledError`。

`Data.uint16(5000)` 与 `Data.int16(5000)` 是不同标签；访问器标签不符抛 `TypeError`。字节入口接受 `bytes / bytearray /` 连续一维单字节 `memoryview`，不接受文本或隐式整数列表。输入和返回均拥有内存，修改原输入不会改变协议快照。

配置通过关键字构造，未知字段报错。时间参数使用有限、非负秒；会话超时及 Engine.poll / IoRuntime.run_for 须为毫秒精度，精度损失报错。执行器延时和虚拟时间按原生时钟刻度处理，支持 RS-485 位时间的小数毫秒。配置字段读取返回副本：

```python
from dlt698 import ClientOptions, SessionOptions

options = ClientOptions(protocol=SessionOptions(request_timeout=2.0))
# 或取出嵌套配置，修改后整体赋回；直接修改 options.protocol 的临时副本不会保存。
protocol = options.protocol
protocol.request_timeout = 3.0
options.protocol = protocol
```

完整 `.pyi` 与 `py.typed` 随 wheel 交付，构造器字段与返回联合类型有明确声明，严格 mypy 校验公开 Python 代码与示例。

## 线程、事件和高级服务

同步客户端建连/请求/断开以及服务器停止等待期间释放 GIL；另一业务线程可以 `request_disconnect()` 或 `request_stop()`。活动对象用 `with`、`close()`、`async with` 确定性收尾；不依赖 GC 执行业务。

同步入口的 `on_event` 由显式 `dispatch_events()` 在调用线程分发。默认队列最多 256 项及 1 MiB 字节，超限丢新事件并累计 `dropped`；事件是诊断观察，不是事务完成。callback 抛异常传播；callback 内关闭后不再分发。库不设置全局日志。

专家 Engine 只接受所属线程访问，禁止在 provider/backend 中重入。`poll()` 默认预算 1 ms；预算不强行中断业务 callback。事务完成队列默认最多 1024 项和 16 MiB 编码消息字节，超限关闭 Engine 并明确报错。Python provider 收到拥有型参数，可安全保存；目录权限、类型校验、异常转 DAR=255 沿用 C++。

`Engine.connect_tcp(..., role=Role.server)` 支持协议服务器主动拨号；
`Engine.listen(..., role=Role.client)` 支持协议客户端接收连接，后者不要求对象目录。
协议服务器始终要求 ObjectRegistry。未传 role 时仍采用拨号客户端、监听服务器的默认值。
`link(type, heartbeat_seconds, connection)`、`release(connection)` 返回完成 token；
`Completion.traffic` 是 Event 副本，含 send/receive、bytes、timestamp、error。
`SessionOptions.calendar_clock` 接收无参函数并返回 model.DateTime，异常由原生会话关闭处理。

低层接口可将 `MemoryChannel.pair(executor)` 或自定义 IChannel/IExecutor 与
`SessionHandle(channel, executor, options)` 组合；通过 `attach_services(session, registry)`
安装原生普通服务。先 start，再持续驱动执行器。异步完成回调接收拥有型成功值或 Error，
void 成功为 None；自定义完成函数只允许调用一次。IoRuntime.run_for 在当前线程推进原生
TCP/串口 I/O，不启动线程。MemoryObject.bind_method / bind_record 可直接注册便利回调。

`SerialLinkOptions.set_transmit` 与 `async_drain` 支持实际 RS-485 驱动控制；
方向控制返回 None/Error，排空函数在最后停止位发出后调用 done(None/Error)。
这些 Python hooks 与日历时钟仅用于调用线程驱动的 Engine/AsyncClient/低层 Session，
托管 Client/Server/AsyncServer 拒绝它们。关闭后继续驱动，直到挂起操作回调排空。
完整扩展点约束及异常处理见 [回调与生命周期约定](feature-gaps.md#回调与生命周期约定)。

`ProxyRouter.bind(tsa, engine.session_at())` 将前六类代理路由到已关联原生 Session；所有相关 Engine 必须持续驱动。第七类透明转发使用 `TransBridge`：传给 `Engine(transparent=bridge)`，应用 `drain()` 取命令，异步设备完成后 `complete(token, ProxyTransResponse(...))` 或完整 `Error`。桥默认最多 128 项、1 MiB 命令字节；取消后的迟到/重复完成返回 False。桥不调用后台 Python；真实透明端口由应用接入。

`SecurityBackend` 子类只能配置在调用线程驱动的 Engine/AsyncClient。同步托管 Client/Server 拒绝 Python 安全后端，避免原生工作线程进入 Python。认证、保护和验证均委托实际后端，默认不提供假认证或伪 ESAM。安全 mock 测试仅验证委托，不代表真实设备安全认证。

子类构造时必须完整实现 8 个后端方法，包括 `reset`。多连接监听配置
`SessionOptions(security_backend_factory=YourBackend)`，每个会话构造时创建独立实例；
不能同时指定 `security_backend` 和工厂，也不能用于预设关联。C++ 会拒绝仍被其他
存活会话持有的实例，包括工厂错误返回同一实例的情况；仅关闭会话不代表其句柄已析构。
`session.validate_session_options(options)` 只做配置校验，不调用工厂、不清理后端。
`reset` 必须返回 None 且幂等；异常通过 `sys.unraisablehook` 报告，不能跨原生 noexcept。

低层 `SessionHandle` 支持普通 GET/记录/SET/ACTION、高级请求、诊断、REPORT、
FollowReport 和 ACD 处理器，`None` 移除处理器。请求参数拥有内存，可以保留；
REPORT 处理器返回 bool，只有 True 才确认。高级处理器接收 `(request, done)`，
`done` 接受拥有型响应或 Error，至多调用一次；处理器可返回幂等、非阻塞取消函数。
驱动抛异常后的迟到完成被忽略，关闭后的结果由原生会话丢弃。

`service.attach_services(session, objects)` 安装普通服务；
`service.attach_advanced_services(session, objects, executor, options, transparent)` 安装
MD5/ThenGet/PROXY，executor 必须与会话一致，transparent 为可选 TransBridge。
`service.ProxyProvider` 支持 Python 子类，实现 `async_request(server, request, done)`
即可接入自定义目标；完成和取消约定与高级处理器相同。所有这些 Python 扩展点必须在
调用线程驱动，不创建 Python 工作线程。首批修复状态见 [审计记录](implementation-audit.md)。

## 示例与构建隔离

`examples/tcp.py`、`async_tcp.py` 展示读取；`async_server.py` 展示 asyncio 服务端；`serial.py` 展示实际串口参数；`provider.py` 展示实时目录。运行示例前先安装包。真实串口/RS-485 的方向、USB 排空及帧间隔必须现场验证；本机没有硬件验收记录。

根 CMake 和 `cpp/` 不查找 Python 或 pybind11。Python 单独从 `python/CMakeLists.txt` 构建，并在自己的构建树静态链接 core/session/service/transport/app；`/utf-8` 沿用原生目标。wheel 不依赖外部 dlt698 DLL/SDK。Windows 仍需系统支持的 MSVC 运行时，Linux 使用 manylinux 2.28 基线。

## 同步检查与发布

唯一版本源是根 `VERSION`。包 metadata、原生扩展和包装层构建记录在 import 时一致性校验；正式构建还要求干净提交、源码摘要和绑定清单摘要。sdist 保存身份并在无 Git 重建前核对源码，拒绝修改后的源码冒充原归档。

`tools/api_contract.py --runtime` 核对公开头文件摘要及实际绑定；`tools/ast_inventory.py` 核对全部公开声明、重载、成员、枚举和默认参数；`tools/generate_stubs.py --check` 核对已安装扩展与冻结类型声明。修改 C++ 公开契约后必须同时审核对应清单、绑定、stub 与相关测试，再显式更新清单。

`.github/workflows/python-ci.yml` 在普通提交中执行安装、向量、类型、格式、AST、C++ 双向互操作及无 Git sdist 重建检查。`release.yml` 使用 `release-matrix.json` 的五个平台、四个 CPython 版本构建 20 个 wheel、1 个 sdist，保存为 Actions artifacts 并供 PyPI 发布。所有 Python 制品必须满足完整矩阵，且同版本、同提交、同源码与 API 摘要，才能上传到 PyPI。GitHub Release 另行发布带公开头文件的 C++ 动静态库压缩包。

Python 单独修复也提升共享 `VERSION` 并重建两端；不使用 Python 专属标签、post 版本或覆盖公开制品。正式发布先公开 C++ GitHub Release，再通过独立任务从本次 Actions artifacts 取得已校验的 wheel 和 sdist 并上传到 PyPI；首次启用需按 [PyPI 发布配置](../docs/releasing.md#自动发布到-pypi) 注册 Trusted Publisher。PR 和手动验证运行仅构建 Actions 产物。非 CPython、free-threaded 和子解释器尚未开放；新平台需共同版本验收后加入。

本地已执行范围与外部待验收项目见 [验证记录](verification.md)。文档中的完整矩阵是 CI 发布目标，不能据此声称本机已经验证 Linux、macOS 或其他解释器。
