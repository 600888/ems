# 公共模块设计与实施规范

> 版本：1.9｜日期：2026-10-24｜状态：部分实现；实时动态库、第三方后端、记录级客户端与基础常驻入口已完成首批，类型化授权门面/管理 RPC 待接入。
> 范围：C++17 公共库、跨进程与前后端数据契约及其与采集、协议、数据管理、历史、存储、Web、策略模块的边界。各节描述目标契约，实际落地范围以对应模块 README 和测试为准。

## 1. 设计决策与实施入口

公共模块保存多个业务模块共同使用的模型、接口和基础能力。**设备属于公共模块**：设备身份、资产关系、标准点、能力及绑定由公共模型统一定义；设备连接、采集任务和控制决策分别由采集、协议和策略模块实现。

后端按公共、采集、协议、实时数据管理、历史数据、存储、Web、策略等逻辑模块组织。逻辑模块不等于操作系统进程：运行采集与上送、实时数据库、存储、策略和 Web 五类业务进程。历史模块组装在存储进程内；实时生命周期组装在 `ems-rtdb`，公共实时库仍由各进程直接调用，不把每次读写改成 RPC。管理边界见[实时数据库进程设计](../processes/data/design.md)。

后续执行本设计时，按第 15 节顺序交付，每阶段满足第 16 节对应验收项后再扩大范围。不要一次性移动所有旧目录，也不要把本文件中的目标路径误认为已有实现。

### 1.1 权威文档及变更规则

| 主题 | 权威位置 | 本文负责内容 |
| --- | --- | --- |
| 进程、数据库、控制闭环 | [系统架构](../architecture/system-design.md) | 公共模块如何支持这些职责 |
| 资产、设备、固有点、能力、点表 | [设备模型](../architecture/device-model.md) | C++ 模型归属、接口及迁移 |
| 通道、协议 profile、通信资源、绑定 | [设备、通道与协议](../architecture/channel-protocol.md) | 公共定义与协议实现的分界 |
| 实时读写、CAS、共享内存和 Redis | [实时访问接口](realtime/access-design.md) | 集成关系与公共类型复用 |
| code、实时物理键与字段 | [Redis 键设计](realtime/redis-key-design.md) | 编码器与跨进程验证 |
| 公共目录、库依赖、配置/数据库/消息接口、实施验收 | 本文 | 公共模块实施基线 |
| 公共数据库访问、ormpp、历史适配与迁移 | [公共数据库访问层](database/) | 数据访问的详细分层与整改基线 |

本文不另建设备点位目录、Redis 键规则或实时存储接口的副本。实现发现冲突时，先修订对应权威专题及受影响契约，再编码；不能在某个适配器里建立隐藏例外。

### 1.2 必须遵守的约束

1. 公共模块不依赖 collector、protocol 的具体驱动、history、storage、strategy 或 web 的业务实现。
2. 公共设备模型不持有 socket、线程、数据库连接、协议句柄或策略对象。
3. 实时状态、配置、遥测接收和业务命令分别使用专门接口，不合并为万能数据库类。
4. MySQL 为配置与业务数据权威源；Redis 默认只提供实时状态，ZeroMQ 承担发布订阅与命令交接。
5. 每个进程独立初始化 ormpp 进程内单例池，各 Repository 复用公共上下文；同一业务写入及其 outbox 使用同一数据库连接的同一事务。
6. 公共库只定义控制意图、命令、回执的类型和持久化接口；策略负责仲裁，采集负责执行。写实时状态不能触发设备动作。
7. 各 C++ 进程共享 schema、样例和语义，不跨进程传递 C++ 内存布局、连接对象或虚函数 ABI；浏览器与后端使用版本化 JSON 契约。
8. 所有等待有期限，所有队列有容量，结果未知与明确未执行必须区分；禁止静默丢弃可靠数据。

## 2. 职责边界

| 子模块 | 公共职责 | 业务模块负责的部分 |
| --- | --- | --- |
| core | 错误、调用上下文、时间、稳定身份基础类型 | 业务错误的决策与恢复流程 |
| model | 资产/设备/测点/能力/绑定定义，纯模型校验 | 设备会话、调度、协议 I/O、控制算法 |
| codec | 模型与契约之间的显式编解码 | 厂家报文、寄存器字节序和协议帧解析 |
| config | 版本配置读取、快照校验、本地不可变视图 | Web 编辑流程、存储进程发布编排、采集/策略应用 |
| database | 连接池/事务、公共 ormpp Repository、历史目录/身份/隔离、时序接口及适配 | 历史采样/聚合规则、报表编排、鉴权、Drogon HTTP 层 |
| realtime | 类型化实时访问、来源防护、CAS 与后端实现 | 来源选主、设备级隔离、策略新鲜度处理决策 |
| messaging | 事件信封、ZeroMQ 遥测与变化提示、命令传输接口 | 业务处理、持久归档和故障处置 |
| observability | 结构化日志、指标接口、健康状态基础类型 | 各进程 HTTP/gRPC 健康端点及就绪判定 |
| util | 有明确复用场景的字符串、时间等小型纯函数 | 通用任务调度框架、任意杂项业务逻辑 |

`database` 集中实现 MySQL/时序数据库访问，含历史目录与 TDengine 适配；业务进程不各自实现数据库连接、映射或 SQL。`history/storage` 保留采样校验、聚合/保留策略、发布和查询编排，通过公共 API 注入依赖。生产仅存储装配时序后端，其他进程经存储服务查询历史；公共模块不依赖存储业务类。详见[数据库访问分层](database/#2-分层与职责)。

告警定义、告警事件数据类型可以共享，告警产生/恢复/确认的业务状态机归策略；权限上下文可以共享，Web 用户认证及权限管理归 Web；共享配置校验不替代请求鉴权。

## 3. 工程目录与命名

为减少无意义搬迁，采用仓库现有的 `backend/include` 与 `backend/src` 分离方式。逻辑上的 `common/model` 对应下面的公共头文件和实现目录，不另外创建与其竞争的 `backend/common`。

```text
backend/
  include/
    common/
      core/                  # result.h、call_context.h、identity.h、time.h
      model/                 # asset.h、device.h、point.h、channel.h、binding.h
                             # capability.h、command.h、alarm.h、validation.h
      codec/                 # model_codec.h、event_codec.h
      config/                # config_snapshot.h、i_config_provider.h
                             # i_config_snapshot_store.h、config_validator.h
      database/              # i_database.h、i_transaction.h、repositories/
        history/             # 历史目录、事件身份及隔离接口与 DTO
        timeseries/          # 后端无关的时序读写接口、类型与工厂配置
      messaging/             # event.h、ZeroMQ 通道接口与编码
      observability/         # logger.h、metrics.h、health.h
      util/
    realtime/                # 沿用实时专题规划的公开路径
  src/common/
    core/
    model/
    codec/
    config/
    db/                      # 沿用现有实现目录；公开 API 位于 common/database
      runtime/               # 进程内连接管理、事务与错误转换
      mysql/                 # ormpp、行实体、迁移映射私有实现
      timeseries/taos/        # TDengine 客户端、映射与读写实现
    realtime/                # core、factory、backends/redis、backends/shm
    messaging/zmq_change_channel.cpp
    observability/
    util/
  src/web/                  # Drogon Controller、Filter、Web Service；复用公共 Repository
  apps/                      # 各 C++ 进程入口和依赖组装，按实施阶段建立
  tests/common/              # model、codec、config、database、messaging
  tests/rtdb/                # 统一 RTDB 契约、业务、分发器及多进程测试
contracts/                   # 仓库根目录，供各 C++ 进程与前端共同使用
  proto/                     # gRPC 与类型定义
  jsonschema/                # 配置、事件和 JSON 映射约束
  fixtures/                  # 有效、无效、兼容版本及边界样例
  realtime/                  # 实时字段与逻辑键黄金样例，引用统一 schema
  compatibility.md           # 当前支持的 schema 范围及兼容策略
backend/data/migrations/     # 沿用已有 MySQL/TAOS 版本化迁移，唯一部署来源
```

命名空间采用 `ems::core`、`ems::model`、`ems::codec`、`ems::config`、`ems::database`、`ems::messaging`、`ems::observability`；实时保持 `ems::rtdb`。类型用 PascalCase，字段与公开方法用 lower_snake_case；实时专题已定义的 camelCase 方法保留，不为风格统一另造一套接口。

新代码包含 `<common/model/device.h>` 等公共路径。旧 `models/`、`db/`、`rtdb/` 头文件仅可作为阶段性转接；转接不能重新声明不同语义的同名模型。

## 4. 编译目标与依赖方向

| Target | 内容 | 允许的主要依赖 |
| --- | --- | --- |
| ems_common_core | 结果、上下文、身份、基础时间 | C++17 标准库 |
| ems_common_model | 共享模型与纯校验 | core |
| ems_common_codec | 契约编解码与精度检查 | model，私有 JSON/Protobuf 实现 |
| ems_config_api / ems_config_core | 配置接口、加载与校验 | model、codec；通过接口注入配置来源 |
| ems_database_api / ems_database_mysql | Repository/事务接口及 ormpp 实现 | model、core；MySQL/ormpp 仅实现侧 |
| ems_timeseries_api / ems_timeseries_taos | 时序接口及公共 TDengine 适配 | core/公共记录；TAOS 客户端仅实现侧，存储入口装配 |
| ems_config_redis / ems_config_mysql | 快照适配及权威版本读取 | config_api、codec；各自数据库客户端 |
| ems_realtime_api / ems_realtime_core | 实时接口与业务门面 | core、model、config_api |
| ems_realtime_client | 统一业务入口、管理发现与内部后端组装 | realtime core/api、管理客户端及已内置适配器；具体后端参数不暴露给业务 |
| ems_realtime_redis / ems_realtime_shm | 实时后端 | realtime_api，私有平台或数据库依赖 |
| ems_messaging_api / ems_zmq_messaging | 事件信封及 ZeroMQ 传输 | model、core；codec/ZeroMQ 仅实现侧 |
| ems_observability | 日志、指标、健康基础能力 | core，复用现有 spdlog 封装 |
| ems_web | Drogon Controller、Filter 与 Web Service | 公共 model/database/config API、Drogon；HTTP 层不进入公共库 |
| ems-rtdb | 后端生命周期、部署描述、接入与授权投影安装 | 公共 realtime/config/database API、管理传输；公共库不得反向依赖此目标 |

上表是目标拆分，不要求每项产出动态库。纯头接口使用 INTERFACE target，普通公共实现首期优先静态链接；旧 `ems_db` 与 DBManager 已删除，调用方直接使用公共数据库 target，不保留兼容转发。

实时数据库交付采用独立例外：`ems_rtdb` 为 SHARED，统一包含公共记录层、配置客户端和已启用的 Boost.Interprocess/redis-plus-plus 适配器。原 ems_realtime_core/shm/redis/client 名称作为链接同一动态库的兼容目标，对外安装为 `ems::rtdb`，不要求业务直接链接第三方 SDK。

业务模块链接所需公共 API；实时访问只使用统一 `RealtimeClient`，具体后端发现和组装由公共实现完成，不在业务入口按 Redis/共享内存分支。工厂显式注册后冻结；没有编译的后端被配置选中时启动失败，不能悄悄替换成其他后端。不要用一个 `ems_common` 聚合 target 强迫协议插件链接 MySQL、Redis 与全部公共能力。

自研目标设置 `CXX_STANDARD 17`、`CXX_STANDARD_REQUIRED YES`、`CXX_EXTENSIONS NO`。每个公开头文件必须可独立包含；model/core 的最小构建不要求安装 Redis、MySQL 或 TDengine。第三方包含路径不能统一 PUBLIC 暴露到所有业务 target。

C++ 虚函数和 STL 接口用于同一构建工具链内的库。协议动态插件继续使用版本化 C ABI；跨进程使用消息/gRPC；共享内存采用实时专题规定的布局。三者均不能直接传递本文中的 C++ 对象。

## 5. 基础类型、结果与时间

### 5.1 共享结果类型

将实时专题第 5 节已经定义的结果/上下文类型提升为 `ems::core`，字段与语义保持一致。在 `ems::rtdb` 中使用类型别名，避免出现两个不能互换的 `Error`。其他公共模块复用同一基础结果模型。

以下为可独立通过 C++17 语法检查的头文件草案；正式实施时拆为 `result.h` 与 `call_context.h`，不表示已有库实现。

```cpp
#pragma once
#include <chrono>
#include <string>
#include <variant>

namespace ems::core {
enum class ErrorCode {
    InvalidArgument, NotFound, Conflict, PermissionDenied,
    NotReady, Unavailable, DeadlineExceeded, ResourceExhausted,
    Unsupported, SchemaMismatch, CorruptData, Internal
};
enum class WriteEffect { NotApplicable, NotApplied, Unknown };
struct Error {
    ErrorCode code;
    WriteEffect effect;
    std::string message;
};
template<class T> using Result = std::variant<T, Error>;
struct Unit {};
using Status = Result<Unit>;
using Deadline = std::chrono::steady_clock::time_point;
struct CallContext {
    Deadline deadline;
    std::string trace_id;
};
} // namespace ems::core
```

成功只通过返回值/Receipt 表达；`WriteEffect` 是失败时的确定性：读取失败为 NotApplicable；能证明没有修改为 NotApplied；可能提交但未收到响应为 Unknown。事务提交、事件发布均遵守此规则。不得把“网络超时”统一映射为“可以重试”。

外部库异常在适配器边界转换为结果；不跨插件或线程边界传播异常。`message` 只作脱敏诊断，不作为业务分支依据。不在公共层返回 HTTP 状态码，Web 再映射。

### 5.2 身份与序列

| 类型/字段 | 约定 |
| --- | --- |
| StationId、AssetId、DeviceId、PointId 等 | 使用独立强类型包装稳定 ID 文本；旧数字 ID 转十进制文本，迁移不重新编号 |
| code | 可读业务编码，格式、保留字、作用域和改名规则引用 Redis 键专题；不是显示名称 |
| ConfigVersion | 不可变配置版本标识，只比较相等；并发发布通过独立 activation revision 条件更新 |
| revision | 实时记录 CAS 不透明令牌，禁止当序号排序 |
| source_epoch、owner_epoch | 分别表示样本来源授权代次、控制所有权代次，不可互换 |
| sequence | 同一已授权来源代次内递增的 uint64；换来源不能直接比较序号 |
| event_id、intent_id、command_id | 生产者在持久受理/事件投递前生成，重试保持不变，数据库唯一约束兜底 |

新增数据库 ID 由统一 ID 分配策略产生，禁止进程各自从 1 计数；首期可使用 MySQL 权威表分配数字 ID。离线生成的事件/命令 ID 使用有碰撞检查的 UUID 文本。ID 内容不用于推导类型、权限或设备层级。

### 5.3 值、质量与时间

`PointValue` 使用 `std::variant<bool, int64_t, uint64_t, double, std::string>`，外层 optional 表示无值。类型、单位和质量由模型定义，编码器校验类型和值的组合；禁止将所有值转 double，禁止 NaN/Inf。枚举状态使用版本化码表，未知码保留原码并标记不确定，不能默认正常。

`quality` 沿用契约中的稳定码表，附带可选原因；`stale` 根据采样时间、新鲜度期限和时钟质量计算。配置禁用、配置未绑定、通信离线、值无效、值陈旧是不同概念，不能折叠成一个 bool。旧代码的 `DataQuality`、`PointQuality` 必须建立明确转换表后迁移。

业务时间统一为 UTC Unix 微秒 int64。持久事件分别保存源时间、接收时间；源时间缺失时采用接收时间并保留缺失原因。进程内超时使用 steady clock；序列化只传业务时间或剩余预算，不能发送 steady_clock 的绝对数值。所有 64 位整数/ID 在 JSON 中使用十进制字符串；Protobuf 按明确字段类型映射，Web 到浏览器仍保持无损编码。

## 6. 设备与其他公共模型

### 6.1 模型集合

| 模型 | 必需内容 | 归属与限制 |
| --- | --- | --- |
| AssetNode | asset_id、station_id、parent_asset_id、code、name、asset_type | 唯一主要父节点，无环，不能跨站挂接 |
| Station / EssUnit | 对应 asset_id；站时区/接入点；单元额定能力 | 单元是调度资源，不固定等同一柜一 PCS |
| Device | device_id、asset_id、type_version_id、model_version_id、厂家/固件信息 | code 从资产节点解析；不维护另一棵通道对象树 |
| DeviceTypeDefinition | 类型版本、标准点目录、能力依赖 | PCS/BMS/电表等目录引用设备模型专题 |
| StandardPointDefinition / Point | 标准语义、值类型、单位、点种类；实例 point_id、asset_id、状态 | telemetry/status/event/command 分开；命令和反馈分点 |
| CapabilityDefinition | capability_code、所需点/反馈、参数及单位约束 | 表达配置能力，不保证当前允许执行 |
| ChannelConfig / ProtocolProfileRef | 通道身份、purpose、role、connection_mode、profile 版本、资源引用 | 不持有运行 session；资源和协议角色独立 |
| DeviceChannelBinding | 设备/通道引用、操作集合、版本、设备协议地址 | 多对多；共享总线地址属于绑定，不属于设备全局属性 |
| VendorPointRoute / PointBinding | 厂家点身份、南向路由、逻辑转换和绑定角色 | 标准点身份独立于通信路径 |
| ForwardingPointMap | 标准点到北向协议地址及方向映射 | 与南向厂家点路由分开 |
| DeviceState / ChannelState / UnitState | 时间、质量、版本及类型化状态字段 | 可复制运行快照，不是执行对象 |
| ControlIntent / Command / CommandResult | 稳定身份、目标能力、期限、配置/所有权、审计上下文 | 状态值沿用系统架构；无执行方法 |
| AlarmDefinition / AlarmEvent | 规则引用、资产、发生/恢复/确认事件和版本 | 状态机与规则引擎在策略模块 |

资产关系和电气拓扑使用独立模型，不能把所有关系都压到 parent_asset_id。电池部件可以有测点而没有独立通信设备。标准采样引用逻辑 point_id；厂家原始值只作为明确的诊断载荷，不能污染标准单位/符号。

### 6.2 静态配置、快照与执行对象分离

```text
Device / DeviceConfig       -> 公共模型：身份和版本化定义
DeviceState                 -> 公共模型：某时刻的状态数据
DeviceSession/ChannelWorker -> 采集模块：任务、连接生命周期、运行资源
ProtocolHandler             -> 协议模块：协议事务和报文
UnitController              -> 策略模块：约束与控制决策
```

公共模型采用值对象、稳定 ID 引用和只读集合，不用双向 shared_ptr 拼出可变对象图。聚合配置在构建时建立索引，发布后通过 `shared_ptr<const ConfigSnapshot>` 共享；读取者持有旧版本期间旧对象保持有效。

模型不继承强制 `to_json/from_json` 的基类，序列化由 codec 承担；不带 ORM 注解或数据库连接方法。数据库行实体可以与模型不同，但必须通过显式映射函数转换，不能让业务使用表字段作为协议配置。

设备业务类型目录可由 `DeviceModel` 基类与 PCS/BMS/消防子类生成；基类只持有类型标识、版本和名称，类型专有的固有点与能力保留在子类。生成后的 `DeviceTypeDefinition` 及实例 `Device` 仍是上述快照中的值对象，不能把多态对象或协议连接放入跨进程契约。

### 6.3 查询与校验接口

首期不增加万能 `DeviceManager`。只读 `ModelView` 从固定版本快照提供下列操作，全部为内存操作：

| 操作 | 输入与输出 | 失败语义 |
| --- | --- | --- |
| find_device | device_id → Device 或 NotFound | 不隐式查询数据库补齐 |
| find_point | point_id → Point 或 NotFound | 不以 point_code 跨设备查找 |
| list_bindings | device_id、operation → 对应绑定列表 | 空列表表示无此操作绑定 |
| resolve_capability | device_id、capability_code → 启用点、命令与反馈定义 | 缺失/禁用依赖给出明确原因 |
| validate_snapshot | 完整候选配置 → ValidationReport | 聚合有上限的字段错误，不返回首个 bool |

`ValidationIssue` 至少含稳定错误码、对象 ID、字段路径、原因；`ValidationReport` 含 issues 和 truncated。校验覆盖身份唯一、资产无环、引用存在、类型/单位、范围、设备/通道多对多、协议声明操作能力、主来源唯一、命令反馈依赖和版本闭合。协议专有配置由受控 schema/校验器验证；通过静态校验不等于驱动已完成现场能力验收。

## 7. 配置访问与版本生效

### 7.1 三种配置

| 类别 | 来源 | 生命周期 |
| --- | --- | --- |
| 进程部署配置 | 配置文件 + 明确允许的环境变量覆盖 | 决定后端、端点、实例身份、队列/池预算；连接/后端变更走受控重启 |
| 业务权威配置 | MySQL 不可变版本及 active 发布元数据 | Web 或其他职责内模块经公共 C++ Repository 按权限修改 |
| 运行快照 | Redis 不可变完整快照 + 进程本地只读对象 | 按精确版本加载、校验、应用，不做权威编辑 |

部署配置解析优先级为显式启动参数 > 白名单环境变量 > 指定配置文件 > 非敏感默认值。缺失密码/密钥不能回落到示例凭据；schema、容量和超时不合法时启动失败。业务配置版本不受环境变量局部覆盖。

### 7.2 接口责任

以下表格固定接口行为；参数类在对应公开头文件中实现，不能用任意 JSON 字符串代替类型化业务接口。全部 I/O 方法接收 `CallContext`，结果使用第 5 节 `Result<T>`。

| 接口/方法 | 输入 | 输出与保证 |
| --- | --- | --- |
| IConfigProvider::load_version | scope、config_version | 完整只读快照；缺失时仅回源同一版本 MySQL |
| IConfigProvider::load_active | scope | 带 activation revision 的 active 清单及其精确版本快照 |
| IConfigSnapshotStore::get | scope、version | 原始不可变快照与 checksum |
| IConfigSnapshotStore::put_if_absent | scope、version、bytes、checksum | 同版本同内容幂等；同版本不同内容 Conflict |
| IConfigSnapshotStore::activate | scope、version、expected_activation_revision | 条件切换 active；并发冲突不能后写覆盖 |
| ConfigValidator::validate | 候选完整快照 | ValidationReport；不执行设备 I/O |
| IConfigApplyReporter::report | instance_id、scope、version、状态、原因 | 写入该实例应用报告；不是全系统应用成功 |

快照封套包含 schema_version、scope、config_version、generated_at、checksum 及引用清单。checksum 采用 SHA-256，计算对象是封套之外的实际 payload UTF-8 字节；发布者保存并发送同一份字节，读取者先校验原字节再解析，不能先重排 JSON 再算校验和。数据库必须能读取同版本发布载荷或按版本化确定性编码规则重建完全一致的字节。哈希用于完整性检查，不替代访问控制。

同一 scope 的 active 清单原子切换；跨 scope 保存显式版本清单，不承诺跨站/跨单元原子生效。消费者对一次操作固定快照，禁止处理中途拼接另一版本的绑定或能力。

### 7.3 发布与应用流程

1. Web/C++ 业务服务校验候选配置，在同一 MySQL 事务内保存不可变版本、发布请求/outbox，并检查编辑基版本。
2. 存储进程发布器读取已提交事件，生成/读取并持久记录发布载荷，写不可变快照，条件切换 active，再发布版本变化事件。请求携带前置 activation revision；重复请求已切到自身目标时视为已完成，过时请求不能覆盖较新发布。
3. 采集/策略收到通知或定期对账后，按精确版本加载并校验完整快照；Redis 缺失时回源该版本，不能改读“最新”。
4. 业务模块准备资源，在安全边界切换本地配置引用，并安装对应来源/配置授权；分别报告 loaded、applied 或 failed。采集的资源准备、listener 启停和在途任务处置由采集层完成。
5. 控制只使用相关参与者均满足的已应用版本；active 已更新而某采集实例失败时，页面保留其失败原因，拒绝依赖新映射的控制。

本地引用切换使用 C++17 对 shared_ptr 的原子操作或显式互斥锁，不使用 C++20 的 `atomic<shared_ptr<T>>`。旧快照只读直到最后一个使用者释放；旧在途采样保留其原 config_version，由来源防护决定能否更新实时值。

回滚通过新的发布操作引用保留的旧内容，并生成新的 activation revision；不篡改已发布版本，不恢复旧 owner_epoch/source_epoch，不自动重放旧命令。无法访问新配置时可继续已应用旧版本的监视，必须显式呈现版本落后；依赖新版本的操作拒绝。

## 8. 公共数据库 API 与事务

### 8.1 接口及连接所有权

| 接口 | 首期行为 |
| --- | --- |
| IDatabase::begin | 在 deadline 内独占取得连接，返回不可复制的 ITransaction |
| ITransaction::commit / rollback | 返回提交 Receipt 或带确定性的 Error；关闭后禁止继续调用 |
| IConfigRepository | 按稳定 ID/版本读取模型、保存不可变版本、乐观并发检查 |
| IControlRepository | 追加意图、执行命令、回执；查询稳定 ID 已有结果；条件推进允许的状态 |
| IAlarmRepository | 保存告警生命周期事件、确认记录及查询投影 |
| IOutboxRepository | 同事务 append；发布端有界 claim、续领/超时接管、mark_published |
| IHistoryCatalogRepository | 按样本作用域/版本批量读取目录和映射，完整校验后标记 ready |
| IHistoryEventRepository | 原子登记/核对事件身份和摘要，持久保存隔离及诊断 |
| ITimeSeriesDatabase | 类型化时序读写、逐项批量结果及有界扫描；独立于 MySQL 事务 |

MySQL Repository 方法显式接收调用方的 `ITransaction&`，由业务服务开启/提交；禁止内部偷偷新建连接或提交事务。事务与其创建的 IDatabase 实例绑定，跨实例传入直接拒绝；需一致读的一组查询使用同一事务和规定的隔离级别。每进程内相同数据库/凭据与用途的 Repository 共用公共运行上下文及连接池，不各建私有连接。

时序接口不接收 MySQL 事务。历史目录/事件账本经公共 ormpp 实现，确认登记提交后再调用公共时序写入；两库没有共同事务。TAOS 失败或结果未知计入缺口，账本不等于归档成功。接口拆分、连接和类型归属见[公共数据库访问层](database/)。

连接池本身支持线程安全借还，单个连接/事务仅由一个线程使用；事务不可跨进程传递。事务析构对未提交工作尝试 rollback，若结果不确定则丢弃连接，不把它归还为健康连接。commit 返回 Unknown 后不再使用该事务，通过原稳定业务 ID 从新连接对账。

首期业务事务使用 MySQL READ COMMITTED，并结合唯一约束、乐观版本或明确的行锁完成并发控制；需要更强快照保证的查询单独声明。不能仅依赖隔离级别名称代替配置冲突、outbox 领取等具体并发设计。

### 8.2 业务与 outbox 原子提交

```text
业务模块验证权限/规则
  -> begin(ctx)
  -> repository.save(tx, stable_business_id, expected_version, data)
  -> outbox.append(tx, same_event_id, envelope)
  -> commit(tx)
  -> 成功才返回持久受理；Unknown 使用原 ID 查询对账
```

outbox 至少保存 event_id、业务关联 ID、scope/topic、schema_version、不可变 payload、created_at、领取者/领取期限、attempts、published_at。event_id 唯一；同 ID 不同载荷属于冲突，不能悄悄覆盖。

发布器通过短事务及条件更新领取，领取包含独立令牌；网络发布不占用长事务。确认发布时校验领取令牌，防止旧领取者覆盖新状态。发布成功后进程崩溃可能重复发布，由消费者幂等处理。`published_at` 表示已投递事件后端，不等于下游归档完成；需持久归档的 outbox 不能只凭此字段删除，保留至业务确认或满足明确的对账保留策略。

业务状态机检查由所属业务服务执行；Repository 提供状态/版本条件更新和审计原子保存，不把“任何状态可覆盖任何状态”的 CRUD 暴露为命令接口。

### 8.3 Schema、错误与限制

数据库 schema 统一由 `backend/data/migrations/` 的版本化部署迁移维护，沿用现有 MySQL/TAOS 脚本；应用不自动建业务表、超级表或升级 schema。时序动态子表仅由公共适配器按已授权映射受控创建。应用检查 schema 版本范围，超出范围拒绝写就绪；滚动升级先增兼容字段，再迁移/切读，最后独立清理。

唯一键冲突映射 Conflict；对象不存在为 NotFound；权限错误为 PermissionDenied；池耗尽/超时与服务器不可用分别返回明确错误。SQL 参数必须绑定；动态排序/列名只能来自白名单，业务接口不接收任意 SQL。

MySQL 普通 CRUD 统一通过公共实现的 ormpp 实体接口；复杂查询/锁/条件更新所需的参数化 SQL 只允许留在公共 MySQL 实现。TDengine 不在固定版 ormpp 的支持范围，官方客户端及方言仅封装于公共时序适配器。业务目录不得包含 ORM/驱动头或维护执行语句，详见[ormpp 与 SQL 规则](database/#4-ormpp-与-sql-使用规则)。

查询必须分页并限制返回数量，批量写入必须有项数/字节上限；整批事务失败不能返回部分成功，逐项提交接口则必须返回逐项结果，两种接口不得混用。连接预算按所有进程实例累加核算。

MySQL 不可用时停止受理和执行依赖持久状态的新常规控制；监视使用已应用配置，采样按有界内存队列预算暂存并显式标记缺口。SQLite 仅在明确的测试/本地用途下实现，不能默认替代权威 MySQL 并宣称事务语义完全相同。

## 9. 实时数据服务

实现严格沿用 [实时访问接口](realtime/access-design.md)，本节只固定集成边界：

- 业务只调用 `RealtimeClient`；公共客户端在内部发现部署、选择适配器并管理连接/映射，不要求业务配置具体后端。
- `RealtimeService` 在客户端内部复用固定配置视图与类型化校验，保持质量、来源授权、配置版本、序号与错误语义一致。
- `IRealtimeStore` 只处理逻辑记录的完整读、CAS、条件删除及逐项批量结果，不包含配置管理、事件队列或历史 SQL。
- 最新值、设备/通道/单元状态及命令查询投影各有明确写角色；普通样本不能修改已批准来源字段。
- `revision` 是并发令牌，来源代次由授权协调角色安装；公共库校验授权，不负责选主或证明旧主已隔离。
- Redis 默认权威实时后端；共享内存为同机可选后端。相同 scope 在一个部署代次只使用一个权威后端，失败时不能自动双写或切空后端。
- 实时数据库进程负责后端生命周期、接入与授权投影安装，各进程独立创建 store 并直接访问后端；实时数据库进程不分配来源所有权，不代理每次读写。
- 同机受信任的 C++ Web 后端通过公共门面直接映射共享段，或连接权威 Redis；布局由适配器隐藏。浏览器仍只访问 Web，跨主机实时访问首期选 Redis。

读到陈旧值仍可返回完整快照及 stale 状态；后端不可用不能伪装成所有设备离线，NotFound 不能伪装成 0。策略与 CAS 前置校验默认直接使用权威状态，不叠加未校验的实时读缓存。

同一个采样的“最新值已处理”和“历史事件已归档”是独立进度。旧来源样本可以补历史而不更新最新值；实时 CAS 判为重复也不能跳过尚未确认的历史投递。

## 10. 消息契约与事件总线

### 10.1 信封与载荷

`EventEnvelope` 至少包含 schema_version、event_type、event_id、trace_id、producer_id、station_id/code、unit_id/code、asset_id、device_path、config_version、event_time、ingest_time。station 事件的 unit 字段为 null，根事件 device_path 为 `[]`；各事件 schema 明确允许为空的身份字段。

| 事件类别 | 专属字段/责任 |
| --- | --- |
| telemetry | point/device/channel 身份、source_id、source_epoch、sequence、value_type/value/unit/quality、source_time |
| control_intent | intent_id、目标能力、请求值、来源、actor、reason、期限；尚未允许执行 |
| command | command_id、intent_id、目标、expected_config_version、owner_epoch、priority、expires_at；仅策略产生 |
| command_result | command_id、状态、协议确认/设备反馈、原因；按状态机追加事件 |
| alarm_event | alarm_id、生命周期事件、规则/资产/确认者及时间 |
| config_changed / config_applied | scope、配置版本、发布 revision 或应用实例与结果 |

type/schema 不支持时必须隔离并告警，不能反序列化为默认值继续执行。ZeroMQ 遥测首期载荷使用版本化 JSON；调用方通过受控 topic/scope 配置路由，不能在业务代码中拼物理帧。

### 10.2 ZeroMQ 遥测接收接口

存储进程的 `IHistoryFeed` 以受控 topic 接收原始 `EventEnvelope`，提供有界 `read(max_items, max_bytes, ctx)`、本地接收/丢弃计数和 `close()`。取出的事件不再由传输层重投；本地投递 token 只用于隔离诊断，不能当作跨进程游标或事件 ID。发布端的成功返回仅表示本地入队，不能证明存储已收到或写库成功。

命令交接另用 ZeroMQ REQ/REP 与 MySQL outbox/inbox。采集执行前通过公共 Repository 条件写入 MySQL 执行状态；数据库不可用时禁止启动新设备写入，已发出的动作不能因入库失败而假定未执行。

### 10.3 容量、坏消息和恢复

ZeroMQ 接收与实时读写连接独立，接收队列、帧长度、读批次和并发处理数有上限。队列溢出或数据库故障增加丢样计数；坏格式或未知版本的已收到事件进入持久隔离区。隔离写失败也计入丢样，PUB/SUB 不会自动重送。命令消息不能自动修改后重新执行；修复后处理须保留原身份、检查有效期和原状态。

## 11. 采样缓冲与恢复边界

不提供独立的本地持久化采样记录，也不使用运行日志保存可重放样本。采样由有界内存队列缓冲，经 ZeroMQ PUB/SUB 送存储进程，收到的事件按稳定 event_id 入库；命令、回执和告警使用 MySQL Repository 与 outbox 保存业务事实。

```text
采样 -> DataDispatcher 有界内存队列（稳定 event_id）
     -> 分别更新最新值、通过 ZeroMQ 发布原始事件
     -> 存储订阅器使用已收到的原始样本入库；失败计入缺口

命令/告警 -> MySQL 业务记录 + outbox 同事务提交
          -> 存储发布器 -> ZeroMQ 命令交接或变化提示
```

采集内存缓冲中的实时更新与遥测发布分别记录结果；实时 CAS 判为重复或旧来源不能跳过原始事件发布。PUB/SUB 无消费者确认；发布端断线、缓冲溢出、存储离线或写库失败都可能丢样。已收到事件的身份账本只用于冲突检查和重复接收时的幂等处理。

采集或存储达到队列上限时按有界策略丢弃或背压，记录可观察的丢弃计数和时间范围；不能无限增长内存，也不能将丢失样本用当前最新值补造历史。进程重启、长时间中断和数据库故障可能形成不可自动恢复的历史缺口。

命令和告警不使用采样的丢弃策略。命令执行前先在 MySQL 条件登记执行身份/状态；重复 command_id 查询已有结果。I/O 已开始但回执尚未持久化时崩溃，恢复后将未决执行视为 unknown，通过新回读/核验确定结果，不盲目重发。执行前登记失败则拒绝新动作；执行后数据库不可用时停止新控制并对账。该数据库状态不是设备动作的原子提交证明。

outbox 按第 8 节保留与对账，不增加文件水位、日志段回收或独立归档确认事件。未完成 outbox 保持原业务 ID 重试；遥测 PUB/SUB 不具备回放能力。

## 12. 可观测性、运行生命周期与资源预算

### 12.1 日志、指标和健康

运行日志继续复用现有 spdlog 及 `backend/src/log/` 封装，公共模块只统一初始化、级别、轮转、异步队列及上下文字段。已有 ILogger 如保留，仅作薄适配，不另建日志框架；运行日志不承担采样补传或控制恢复。

结构化日志统一包含 process_name、instance_id、module、trace_id，以及适用的 station/unit/device/point、config_version、event_id/command_id、error_code。密码、令牌、连接串凭据和原始认证载荷不输出。后台异步日志队列有界；业务审计不依赖调试日志保存。

至少提供以下指标：池等待/占用、操作延迟/错误/Unknown 数、配置 active/applied 差异、实时冲突/陈旧/旧来源拒绝、ZeroMQ 发布拒绝/接收队列丢弃/写库失败、outbox 最旧未完成时长、采样缓冲占用/最老年龄/丢弃数量/缺口范围。按进程/模块/后端等低基数字段聚合，device_id、event_id 不作为默认指标标签。

健康分 liveness、readiness、degraded，不能仅以线程存活宣称就绪。诊断必须区分：设备离线、配置未应用、实时后端不可用、历史积压、控制链路不就绪。公共模块返回组件健康，进程决定最终业务就绪。

### 12.2 生命周期

```text
启动：部署配置校验 -> 日志/指标 -> 本进程数据库与业务状态恢复
    -> 精确配置加载 -> 实时/事件后端初始化 -> 来源/配置授权核验
    -> 业务模块准备与应用 -> 标记对应能力就绪

停止：撤销就绪/停止接收新任务 -> 停止新发布和读取
    -> 业务处理在途任务、持久记录未知结果 -> 有界排空采样队列
    -> 等待有界后台任务结束 -> 关闭总线/实时/数据库 -> 关闭日志
```

无依赖的初始化可并行，但业务不能在所需依赖未就绪时启动。关闭超时后报告未送出采样数量；内存样本不保证重启恢复，持久业务记录通过 MySQL/outbox 对账。析构不能无限等待。后台任务归创建者所有，不创建无法 join 的分离线程；回调不得持内部锁调用用户逻辑。

### 12.3 必填预算与配置验证

部署模板必须明确 operation_timeout、pool_acquire_timeout、pool_size、max_batch_items、max_payload_bytes、max_queue_items/bytes、shutdown_timeout。单项超时不得超出调用剩余预算；所有池/队列预算按进程实例累计。

具体数值由站点规模、采样频率与目标硬件容量测试确定，不能把示例值当 SLA。实施阶段提交可运行的开发模板，以及记录设备/点数、吞吐、P95/P99 延迟、CPU/内存/磁盘和恢复时间的基线报告；未知预算不得用无限值代替。

## 13. 跨进程与前后端契约及兼容升级

`contracts` 为五类 C++ 进程和 Vue 前端的共同输入。事件/管理服务使用版本化消息、JSON Schema 与 Protobuf，共享内存由公共库解释兼容布局，不直接序列化 C++ 对象；浏览器 JSON 与后端类型映射必须通过相同 fixtures 校验精度、null、时间、code、键、事件和版本规则。

契约交付至少包括：模型/事件 JSON Schema、Protobuf、C++/TypeScript 字段映射说明、合法/非法样例、枚举码表、schema 支持范围和变更记录。Protobuf 废弃字段保留编号，不能复用。JSON Schema 与 Protobuf 若无法自动生成同源定义，必须由同一批黄金样例检测漂移。

同 major 下只允许有明确默认语义的兼容可选字段；字段重命名、类型/单位/枚举含义改变、新必需字段均升级不兼容版本。读取器可以忽略允许的未知可选字段，但不能忽略未知控制动作、未知权限值或不支持的 major。重放/转发保存原始载荷，避免旧读取器丢掉新字段后重新发布。

兼容升级顺序：先部署可读新旧格式的消费者，再部署新格式生产者；未完成 outbox 对账后才移除旧命令读取支持。数据库、事件 schema、配置 schema、共享内存 layout、协议插件 ABI 是独立版本，不能混用一个整数。

## 14. 现有代码迁移对照

下表依据当前仓库路径，不代表旧接口满足新语义。先建立新模块及转换测试，再逐个切换调用方。

| 当前路径/内容 | 目标归属 | 迁移重点 |
| --- | --- | --- |
| backend/src/models/base.h | common/core + common/model + codec | 拆分身份/枚举与 JSON 基类，保留旧枚举值转换表 |
| backend/src/models/device.h、device_group.h | common/model/asset、device | 使用资产归属；设备通过 binding 引用通道 |
| backend/src/models/channel.h、slave.h | common/model/channel、binding | 从站地址迁到绑定，消除单设备通道假设；运行连接归采集 |
| backend/src/models/point_runtime.hpp | common/model/point + realtime | 分离配置、实时值、厂家解码；保留值类型/质量/时间 |
| backend/src/models/point_mapping.h | 对应标准绑定模型或策略派生点配置 | 现有公式映射不等于厂家点绑定，不能直接改名合并 |
| 原 backend/include/db/*.h（已删除） | common/database/configuration 公共记录与仓储 + 公共 ORM 实现 | 参数加载、点表导入和测试已直接替换，不保留旧别名 |
| backend/src/common/db/db_manager.cpp | database_mysql 与各 Repository | 拆开池/事务、业务 CRUD 和旧建表逻辑；历史业务编排留存储，数据库调用统一公共层 |
| backend/src/scd/mysql_history.cpp | src/common/db/mysql + 公共 history API | 迁移实体、反射、查询与事件/目录持久化，复用公共连接/事务 |
| backend/src/scd/taos_series.cpp、time_series_factory.cpp | src/common/db/timeseries + 公共 timeseries API | 客户端、方言、映射和工厂迁入公共层，存储只注入接口 |
| backend/src/rtdb/model/rtdb_device.h、rtdb_channel.h、rtdb_config.h | common/model + config | 配置定义脱离 rtdb，统一设备/通道/点定义 |
| backend/src/rtdb/model/、client/ | 统一 RTDB 模型、缓存和业务客户端 | 原有业务接口复用统一记录存储，来源授权及配置发布另行接入 |
| backend/src/net/para_loader/* | collector 配置适配 + common/config | 读取和校验复用公共库；通道准备/应用留采集 |
| backend/src/net/collect/data_dispatcher.* | collector | 注入实时/事件接口，管理有界内存队列，保留分发编排责任 |
| backend/src/net/protocol/* | protocol | 保留插件边界，不搬到 common；只复用纯模型/契约 |
| backend/src/net/protocol/modbus/proto_modbus_util.hpp、modbus_mapping_map.hpp | protocol/modbus | 协议专用工具移出通用工具 |
| backend/src/log/*、通用 util | observability / util | 按实际复用迁移，不把所有旧工具整体照搬 |

旧数据中的 ID、时间单位、质量码、设备类型、code 和绑定必须先导出映射报告。旧 YC/YX/YK/YT 保留导入转换：映射为标准 telemetry/status/command，并把反馈独立出来；不能仅改枚举名称。

迁移期间每个 scope 只有一个权威配置/实时写入路径。新旧数据格式需要并存时，新格式先以只读影子验证；切换前停止旧写、对齐配置/水位和来源授权，再启用新写。禁止两个模型体系各写各的 Redis 键后长期依赖同步。

回滚分代码回滚与数据回滚：保留迁移前备份、不可变配置、未完成 outbox 和转换清单；存在新格式数据时，只有兼容读取器可回滚上线。不可逆删列/删除旧索引延迟到独立清理阶段，未完成对账不清除原始数据。

## 15. 分阶段实施任务

| 阶段 | 必须产出 | 完成门槛 |
| --- | --- | --- |
| P0 基线与契约 | 现状依赖/数据清单；新目录与 CMake targets；C++17 依赖锁定；身份/枚举/时间转换表；contracts 初版及开发预算模板 | core/model 无数据库依赖构建；Linux/ARM 工具链检查；旧数据转换无未解释项 |
| P1 模型与编码 | core、model、codec、ModelView、ValidationReport；设备/绑定/点/能力 fixtures | 多对多、树/引用/类型校验、无效值和 64 位整数往返通过 |
| P2 数据库与配置 | 公共池/事务、ormpp Repository、历史目录/事件仓储、公共时序适配、统一迁移；配置与 outbox | 同事务原子性、跨 Repository 复用、历史语义兼容、并发/Unknown 与故障缺口通过 |
| P3 实时服务 | 按实时专题实现门面与 Redis 默认后端，按实时数据库进程设计补生命周期、部署与接入，迁移 rtdb 调用 | 来源/CAS/完整替换/Unknown/后端恢复契约通过；管理重启复用有效后端，设备定义已脱离 rtdb |
| P4 事件链路 | ZeroMQ 遥测发布/订阅、持久隔离、采集有界缓冲；MySQL 执行状态/outbox 接入 | 重复、断线、队列溢出、数据库故障与丢样报告通过 |
| P5 业务集成与旧代码退出 | 数据管理入口与采集/策略/存储组装；Drogon Web 接入公共 Repository 与直接实时访问；逐项关闭旧接口 | 五类进程最小端到端链路通过，管理 RPC 不转发正常数据读写，旧模型不再被生产路径引用 |
| P6 可选后端与容量验收 | 共享内存多进程直接访问、管理重启与受控切换；目标现场负载与恢复报告 | 共享内存契约/崩溃恢复、旧使用者隔离通过；性能/RPO/RTO 有实测记录 |

P4 的事件测试可用测试替身，但不能据此宣称历史链路完成。P5 必须接入存储进程真实归档路径验证幂等落库与丢样边界，并演练尚未发布的内存采样丢失场景。共享内存是规划能力；默认 Redis 实时版本可在 P5 验收后交付，共享内存部署须等待 P6。

每阶段交付清单包括：实现代码、被影响调用方、契约/迁移变化、运行命令、测试结果、未完成项和回滚步骤。完成阶段后更新本文状态或追加实施记录，不能仅以“类已创建、编译通过”标为完成。

## 16. 验收矩阵与完成定义

| 编号 | 验收场景 | 必须观察到的结果 |
| --- | --- | --- |
| C01 | 独立包含全部公开头，core/model 最小构建 | C++17 通过；无 Redis/MySQL/协议驱动隐式依赖 |
| C02 | 同设备多通道、多设备共享总线、多单元同名 pcs01 | 稳定身份/绑定准确，不按全局 device_code 错误合并 |
| C03 | 资产环、跨站父节点、缺失引用、禁用能力、命令缺反馈 | 校验给出对象/字段/原因，未配置或无效值不变成 0 |
| C04 | int64/uint64 边界、null、坏质量、UTC 微秒、未知枚举 | C++/TypeScript fixtures 一致，无浮点精度损失 |
| C05 | 事务中业务写成功、outbox 写失败；提交响应丢失 | 前者全部回滚；后者 Unknown，按原 ID 对账，无重复业务效果 |
| C06 | 两发布者同基版本并发、快照缺失/校验失败、旧发布迟到 | 一方条件失败；不拼版本；旧发布不回退新 active |
| C07 | 配置已发布但采集应用失败，随后回滚 | 区分发布/应用；新映射控制拒绝；回滚不重放旧命令 |
| C08 | 多进程 CAS、重复/旧序号、来源切换、完整字段删除 | 遵循实时专题，旧来源不覆盖新状态，逐项结果正确 |
| C09 | PUB/SUB 发布端或订阅端晚启动、断线 | 遥测缺口可观察，后续新样本继续；不宣称自动重放 |
| C10 | 未知版本/坏消息，隔离数据库不可用 | 已收到事件的隔离错误可定位，失败计入丢样，不无限占内存 |
| C11 | 发布前进程退出、Redis 中断、采样缓冲耗尽 | 显式报告可观察的丢弃/缺口；不承诺内存样本跨重启恢复，不伪造历史 |
| C12 | 同一事件再次发布、同 ID 异载荷 | 相同身份幂等；冲突被隔离；不依赖消费 ACK |
| C13 | Redis/MySQL 中断、池/队列满、关闭超时 | 有界退避/背压和明确错误；持久业务可对账，未发布内存采样可能丢失 |
| C14 | 更新实时成功但发布失败，或发布成功而实时被拒绝 | 两条进度独立恢复；历史保存原样本，快照不回退 |
| C15 | Web/北向控制到策略、采集、回执和归档 | 统一仲裁；重复/过期/旧所有权拒绝；Unknown 不盲目重发 |
| C16 | Web 与其他 C++ 进程同表读写、滚动升级与旧事件重放 | schema/枚举/事件兼容；同事务约束和稳定身份不变 |
| C17 | 多进程共享内存、拥有锁的进程崩溃、重建段 | 满足实时专题；Web 走 gRPC；新代次拒绝旧授权 |
| C18 | 现场负载、积压恢复、查询与控制并发 | 报告预算与 P95/P99、资源峰值、恢复水位及实测 RPO/RTO |

默认 Redis 公共模块完成定义：P0–P5 完成，C01–C16 通过，C18 对默认部署有报告；C17 为共享内存交付门槛，不能在未实现时标通过。端到端测试需要尚未实现的策略/存储/Web 能力时，明确标依赖未完成，不用 mock 结果冒充集成验收。

文档本身验收包括：相对链接存在、导航 JSON 可解析、代码围栏配对、C++ 草案可通过 C++17 语法检查，与权威专题无冲突。正式文档站渲染、Linux/ARM 构建、数据库/多进程故障测试分别记录，不能相互替代。

## 17. 实施记录

### 2026-10-22 公共数据库层首批实现

按[公共数据库访问层设计](database/#71-首批实现记录)，已接入 ormpp 进程内连接池、公共显式事务和历史目录/事件仓储，并将时序接口与 TDengine 适配迁出 scd。各进程独立初始化池，公共层只适配事务与错误，不自建池队列。存储业务通过注入公共接口完成目录校验、事件登记及查询编排；提交结果未知时禁止继续写时序库。

旧 DBManager 及调用方已直接替换为公共工程配置仓储；版本配置发布、控制/告警/用户/outbox 的具体 Repository、历史目录发布及真实数据库联调仍待完成，P2 不标记为整体通过。已迁移代码和测试范围以公共数据库专题及 scd README 为准。

### 2026-09-19 P0/P1 公共模块基线落地

- 新增 `backend/include/common/`：core（result/call_context/identity/time，纯 C++17）、model（asset/device/point/channel/binding/capability/command/alarm/validation/catalog/model_view）、codec（model_codec/event_codec，nlohmann 为私有实现）、config、database、messaging、observability 接口头文件。
- 新增 `backend/src/common/model/`（validation.cpp、model_view.cpp）、`backend/src/common/codec/`（model_codec.cpp、event_codec.cpp），实现纯校验与 JSON 编解码。
- CMake targets：ems_common_core（INTERFACE）、ems_common_model（STATIC）、ems_messaging_api（INTERFACE）、ems_common_codec（STATIC）、ems_config_api/ems_database_api/ems_observability（INTERFACE）；公共目标统一 CXX_STANDARD 17。
- 测试：`backend/tests/common/`（test_core/test_model/test_codec，60 用例）覆盖身份/结果/时间、资产环/跨站/引用/绑定/反馈校验、多对多与共享总线、64 位整数与事件信封往返；MinGW g++ C++17 全部通过（C01/C02/C03/C04 局部覆盖）。
- 未完成项：contracts 目录与黄金 fixtures、MySQL 迁移与 Repository 实现（P2）、实时后端（P3）、Streams 实现（P4）、Drogon Web 接入（P5）、Linux/ARM 工具链构建检查。旧 models/、db/、rtdb/ 迁移按第 14 节另行走。

### 2026-09-19 目录规范化与第二批实现

- 目录规范化：`backend/inc` 重命名为 `backend/include`，同步更新全部 CMake 引用（12 处）与文档（design.md、system-design.md、access-design.md）。
- realtime API：按实时专题第 5 节草案落地 `include/rtdb/store/realtime_store.h`（IRealtimeStore/RealtimeStoreBase 批量默认实现）与 `include/rtdb/store/backend_factory.h`（注册-冻结-Unsupported 语义），`src/rtdb/store/realtime_store.cpp` 实现 RecordKey 公共验证器；ems_realtime_api/ems_realtime_core 为统一动态库的接口或兼容目标。
- config core：`src/common/config/` 新增 CatalogValidator（复用 model 纯校验），target ems_config_core。
- 当时测试报告为 82 用例，包含实时 API 和后来移除的本地文件记录测试；该数量是历史记录，不代表当前用例数。
- 修复：realtime Revision 缺判等运算符；当时文件记录实现的修复随模块移除，不再作为当前交付内容。
- 未完成项同上，另加：SharedMemoryStore/RedisStore 后端（P3/P6）、Streams adapter（P4）。

### 2026-09-19 移除 journal

- 按需求移除独立的 journal 接口、文件实现、构建目标、专属测试和归档确认事件，事件编号 7 保留不复用。
- 采样采用有界内存缓冲与 Streams 消费确认；命令、回执、告警继续通过 MySQL/outbox 持久化，运行日志继续复用 spdlog。
- 上述新增事件/控制恢复流程是调整后的目标设计，尚未实现的 Streams、Repository/outbox 集成仍按 P2/P4/P5 验收。
- 本次验证：移除后剩余公共模块与新增废弃事件拒绝用例使用 MinGW g++ C++17 编译，72 个测试通过；这不等同于完整后端或 Linux/ARM 集成验收。
