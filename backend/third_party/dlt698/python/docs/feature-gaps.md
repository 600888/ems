# Python 九类功能缺口补全记录

2026-10-07。上一轮审计确认的九类接口缺口已补齐，协议算法、状态机和 I/O 均复用 C++。
Python 不创建工作线程，不使用 Thread、线程池、to_thread 或 run_in_executor。

| 原缺口 | 已实现入口 | 验证范围 |
| --- | --- | --- |
| 异步客户端串口 | AsyncClient.open_serial / open_serial_configured；Engine.open_serial | 原生端口打开、SerialLinkChannel、LINK/CONNECT 共用连接流程；不存在端口及 1.5 停止位错误后可重试 TCP。成功串口收发待硬件验收。 |
| 协商能力、读取规划、候选点探测 | standard 的 Support、ReadService、Capabilities、ReadBatch、CandidatePoint 及六个能力/规划函数；service 的 ProbeOptions、PointResult、async_probe_points；Engine / AsyncClient.probe_points | 未知能力按 Normal 读取，List 回退、顺序和重复点保留；Data、DAR、事务错误、标准校验错误分别保存。 |
| 记录独立校验 | standard.validate_record_time / validate_record_query / validate_record_cell / validate_record_result | 合法记录、无效日期、类型错误和行列数量错误，复用原生错误分类。 |
| 协议日历时钟 | SessionOptions.calendar_clock，返回 model.DateTime | Engine、AsyncClient、低层 SessionHandle 支持；回调在所属驱动线程执行，失败沿用原生 invalid_value/关闭行为。 |
| 协议角色独立于 TCP 方向 | Engine.connect_tcp / listen 的可选 role 参数；低层通道和 SessionHandle 自由组合 | 协议服务器拨号、协议客户端监听，公共与预设连接均可读取；默认角色保留原兼容行为。 |
| LINK/RELEASE 和收发观察 | Engine.link / release，Completion.traffic；SessionHandle.async_link / async_release / set_traffic_handler；AsyncClient.disconnect | 心跳、应用释放、拥有型原始收发字节与 Unix 时间；disconnect 后可重连，空闲 aclose 优先 RELEASE，取消仍强制收尾。 |
| MemoryObject 便利回调 | MemoryObject.bind_method / bind_record | 方法和记录回调复用目录错误处理；传入拥有型参数，可保留，目录持有 provider 生命周期。 |
| 低层通道、执行器、手动 RS-485 | common.IExecutor / ITimer / ManualExecutor；transport.IChannel / MemoryChannel / IoRuntime / TcpChannel / TcpListener / SerialChannel / SerialLinkChannel；SessionHandle 构造器及 attach_services；SerialLinkOptions.set_transmit / async_drain | 原生虚拟时钟、缓存预算、关闭、计时器取消、Python 子类保活、真实排空完成等待、恢复接收、重复及迟到完成隔离。 |
| 独立分片/重组和 GET 分块 | protocol.link 的 Fragment / FragmentType / Reassembly / LinkFragmenter / LinkReassembler / encode_fragment / decode_fragment；protocol.apdu.GetBlockTransfer | 乱序、重复片、资源上限、末片交付，GET 有序收集、重复/跳号拒绝及远端 DAR。 |

## 回调与生命周期约定

调用线程驱动的接口允许 Python 业务回调。Engine/Endpoint 由应用持续 poll，AsyncClient
由所属 asyncio loop pump；低层 SessionHandle 由应用提供的串行执行器驱动。
IExecutor.post/schedule 必须排队，不能在投递时内联执行任务，须隔离任务异常；
now 返回单调秒数，is_current 判断是否在该执行环境内。所有扩展对象及操作应在同一驱动线程使用。

原生到 Python 的低层完成回调接收成功值或 common.Error；void 成功为 None。
自定义 IChannel 的完成函数和 async_drain 的 done 同样接受成功值/None 或 Error，
只允许交付一次；驱动抛异常后的晚到完成不会二次通知事务。close 必须完成挂起读取/写入，
timer.cancel 必须禁止尚未执行的任务；错误的 close/cancel 以及 noexcept 查询异常通过
sys.unraisablehook 报告，不能作为成功关闭或有效执行器使用。
退出前关闭会话、通道和监听器，并继续驱动直到完成回调排空。

手动 RS-485 的 set_transmit(True/False) 返回 None 或 Error；配置方向控制时必须有
async_drain(done)，确认最后停止位已发出后调用 done(None)。适配器再执行 33 位静默间隔。
Python hooks 可用于 AsyncClient、Engine 和 SerialLinkChannel；托管 Client/Server/AsyncServer
在 C++ 工作线程运行，明确拒绝 Python 日历/安全/串口 hooks。该限制防止后台线程进入解释器，
并不限制原生 C++ app 使用 C++ 回调。

## 尚需验收的范围

上述九类不再是 API 缺口。真实串口、RS-485 方向控制和 USB 排空、真实 ESAM 仍需设备验收；
内存通道、串口失败收尾、TCP 回环和安全 mock 不能替代硬件验证。
真实安全后端目前只有扩展合约，没有内置认证、密码算法或 ESAM/厂商 SDK 适配实现，
因此它属于尚缺具体实现，不能仅标为待硬件验收。完整剩余范围见
[未覆盖功能审计](implementation-audit.md)。
其中安全后端会话隔离、低层 Session 处理器、自定义代理和 raw Session 高级服务入口
已在首批修复中补齐；真实密码后端仍需具体实现。
本机验证 Windows x64 / CPython 3.11；其他平台和 CPython 3.12–3.14 由 CI 矩阵验收。
free-threaded、子解释器及非 CPython 仍不在支持范围。

高层 AsyncServer 继续复用 C++ app::Server 的共享 Device；动态 provider、高级 PROXY/REPORT、
透明桥和安全后端通过 Engine/Endpoint 专家接口使用。尚未提供统一的 asyncio 专家服务端包装器，
现有原生专家能力仍需应用驱动。详细结果见 [验证记录](verification.md)。
