# Python 实施与验证记录

执行环境：Windows x64、CPython 3.11.6、MSVC 19.39、CMake 3.30.5；2026-10-07。

## 未覆盖功能首批修复验证（2026-10-08）

本批完成 G02、G04、G05、G06：安全后端工厂及会话独占生命周期、9 个低层 Session
处理器及执行器线程查询、raw Session 高级服务安装、自定义代理提供者。
SecurityBackend 在构造时检查全部 8 个必需方法，reset 异常报告到 unraisablehook，
verify_connect/reset 要求返回 None。真实密码/ESAM 仍缺具体后端，实现状态见
[当前审计](implementation-audit.md)。

- 实际新版 wheel 安装后的完整 Python 回归：145/145 通过，包括独立 C++ 双向互通。
- 新增覆盖安全工厂预校验无副作用、重复后端拒绝且不清理其他会话、实例销毁后复用、
  双 TCP 连接隔离、清理异常、处理器移除、REPORT 确认决策、代理保活、重复/迟到完成、
  原生错误字段恢复以及高级服务安装后的 MD5/PROXY。
- 独立 C++ 静态构建及 CTest：11/11 通过，包含 TCP、app 和重定位安装消费方。
- 干净虚拟环境安装新版 wheel，全部公开模块导出、原生 asyncio TCP 和服务端重启通过；
  禁止 Thread.start/to_thread/run_in_executor 后仍通过，无新增 Python 工作线程。
- 严格 mypy：33 个源码/示例；Ruff lint/format：75 个文件；
  159 个项目自身 C++ 源文件 clang-format dry-run 通过，不处理 third/build。
- API/runtime：43 个公开头文件、925 条契约记录；AST：1300 条公开声明；
  实际安装扩展与冻结 stub 一致性检查通过。
- wheel/sdist 内容、类型声明、许可证及源码/API/版本/提交身份一致性检查通过。

本批开发制品位于 `build/python-fix-dist/`：`dlt698-0.1.0-cp311-cp311-win_amd64.whl`
及 `dlt698-0.1.0.tar.gz`。G01、G03、G07—G14 仍保留；下面各节为此前验证记录，
不据此扩大平台、解释器或硬件支持范围。

## 九类功能缺口补全验证

九类审计缺口均已开放原生实现，入口与回调约定见 [补全记录](feature-gaps.md)。
新增异步串口、能力/读取规划/点位探测、记录校验、协议日历时钟、TCP 方向与角色分离、
LINK/RELEASE/流量快照、MemoryObject 方法/记录回调、低层执行器/通道/RS-485 hooks、
独立链路分片及 GET 分块工具。Python 不创建工作线程；托管后台入口拒绝 Python hooks，
Python 回调通过所属线程驱动的 Engine/AsyncClient/低层 Session 使用。

- 最终实际 wheel 安装后完整 Python 回归与独立 C++/Python 双向互操作：135/135 通过。
- 新增覆盖未知/否定能力及 List 回退、重复点、Data/DAR/事务/结构诊断、四种记录校验、
  协议角色反转、日历异常、拥有型收发事件、RELEASE 后重连及竞争拒绝、
  探测断开时旧完成与复用 token 隔离、自定义执行器/计时器/通道保活和 GIL、
  一次性与迟到完成、RS-485 真实排空等待/关闭/小数毫秒、低层 TCP、分片乱序/重复/预算和 GET 分块。
- 禁止 Python Thread.start/to_thread/run_in_executor 下，AsyncClient/AsyncServer 仍可通信、探测和收尾。
- 无系统 site-packages 的干净虚拟环境安装最终 wheel：全部公开模块导出、原生 asyncio TCP、
  重启和禁止 Python 工作线程的冒烟检查通过。
- 独立 C++ 静态构建及 CTest：11/11 通过，包括 TCP、app、重定位安装消费方。
- 严格 mypy：33 个公开源码/示例；Ruff lint/format：74 个文件；
  159 个项目自身 C++ 源文件 clang-format dry-run 通过，不处理 third/build。
- 43 个公开头文件、1298 个 AST 声明、908 个 API 清单条目及实际导出核对通过。
  生成工具重复运行无差异，已安装扩展与冻结 stub 一致性通过。
- wheel/sdist 压缩包、类型声明、许可证、无 SDK 混入及源码/API/版本/提交身份一致性通过。

本次开发制品位于 `build/python-gap-dist/`：`dlt698-0.1.0-cp311-cp311-win_amd64.whl`
和 `dlt698-0.1.0.tar.gz`。真实串口只验证失败收尾，成功收发及 RS-485/ESAM 仍需硬件；
平台/解释器兼容范围与正式发布矩阵不据此扩大。下面保留之前两轮验证记录。

## 目录分层与异步服务端增量验证

本次按业务整理源码、原生绑定与测试目录；AsyncServer 复用 C++ app::Server，
启停等待线程由 C++ ServerRunner 管理，Python 不创建工作线程。

- 原生扩展重编译与 editable 安装通过；修复 editable 身份记录的定位。
- 完整 Python 回归与独立 C++/Python 双向互操作共 115 项通过。
  新增覆盖三种关联场景、多客户端、连接预算、启动失败重试、停止后重启、取消启动、
  取消关闭、跨 loop 拒绝、回调异常、回调内关闭及启动未完成时请求停止/关闭。
- 禁止 Python Thread.start/to_thread/run_in_executor 的测试下，服务器仍可启停和通信。
- 严格 mypy、Ruff lint/format、43 个公开头文件/API 契约和 stub 一致性检查通过。
- 155 个项目 C++ 源文件的 clang-format dry-run 通过，不处理 third 或构建目录。

异步服务端串口入口验证了不存在端口的错误传播与失败后资源收尾；成功收发仍需硬件。
功能差异及后续优先级见 [功能差异审计](feature-gaps.md)。下表保留此前验证记录。

| 项目 | 本地结果 |
| --- | --- |
| Python 实际 wheel 安装测试 | 98 项通过：规范向量、数据边界、普通/列表读写、provider、MD5/ThenGet、记录分块/投影、REPORT、PROXY、透明转发及迟到完成隔离、mock 安全、错误身份、GIL 并发取消与 asyncio task/loop 收尾 |
| 干净环境安装 | 无系统 site-packages 的新虚拟环境、空工作目录：全部公开模块导出、原生 TCP 读写及资源收尾通过；最终 wheel 再次通过全部 98 项测试 |
| 独立 C++ 动态根构建 | `build/traffic-msvc`：11/11 CTest 通过，含 TCP/app 和重定位安装消费方 |
| 独立 C++ 静态直接 cpp 构建 | `build/python-cpp-core` transport ON：11/11 通过，含重定位安装消费方 |
| 独立 C++ 无 transport 核心 | 同一直接 cpp 构建先以 transport OFF 配置：8/8 通过，含安装消费方；随后开启 transport 验证静态完整栈 |
| 双向跨语言互操作 | 独立 SDK 消费方 `build/python-interop/Release/dlt698_interop.exe`：Python client→C++ server、C++ client→Python server 通过 |
| 公开契约 | 43 个头文件、1298 个公开 AST 声明；对应清单及原生导出核对通过 |
| 类型、风格 | 严格 mypy、Ruff lint/format、项目 C++ clang-format dry-run 通过；不处理 third/build |
| sdist | 实际白名单压缩包检查通过；无 Git 解压目录可成功重建 wheel；归档与 wheel 版本/源码/API 身份一致 |
| Windows 动态依赖 | dumpbin 仅发现 Python、Winsock、Windows 系统及 MSVC/CRT 运行时，无 dlt698 DLL/开发机 SDK 依赖 |
| 构建缓存 | 捕获固定 sdist 时间戳导致旧对象复用的问题；Python 专属私有编译参数加入源码摘要，输入变化同时重编核心和绑定，独立 C++ 配置不变 |
| 联合门槛 | 完整矩阵、缺少 wheel、混合提交、C++ 制品篡改的测试通过；配置正式发布前必须通过完整 CI |

此前 P0–P4 已实现；本轮进一步补齐异步客户端串口、低层自定义 executor/channel、手动 RS-485 hooks 等九类接口。新 ABI 和 PyPI 推广仍不在支持范围；普通 API、版本及内核继续复用。

前一轮本地可安装制品位于 `build/python-final-dist/`：`dlt698-0.1.0-cp311-cp311-win_amd64.whl` 和 `dlt698-0.1.0.tar.gz`。两者为 development 制品，版本、提交、源码摘要和 API 摘要相同；最终 wheel 从无 Git 的 sdist 目录重建，并在曾触发旧对象复用的同一缓存中验证修复。

尚未在本机验证：Linux x86_64/aarch64、macOS x86_64/arm64、CPython 3.12–3.14、真实串口/RS-485、真实 ESAM。平台及解释器已有 CI 矩阵，硬件和安全需要实际设备。尚未创建标签、公开 Release 或推广至 PyPI。

最初复用的旧 Ninja 缓存缺少正确 VS SDK 环境，导致安装消费方构建失败；补实际 VS 环境后动态构建通过。旧静态缓存存在陈旧链接对象，因此以新建的独立 VS 直接 cpp 构建完成静态与无 transport 回归。最终通过结果以上表为准。
