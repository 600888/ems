# EMS Redis 键设计

> 版本：1.6｜更新日期：2026-09-29｜状态：目标设计，待实现与迁移。
> 本文是 Redis 实时键命名与数据组织的约定。整体职责见 [EMS 系统架构设计](../../architecture/system-design.md)，遥测与配置入口见 [实时数据与配置](index.md)。四类 C++ 进程复用公共 ormpp API，并各自管理数据库连接。

## 1. 评估与决策

本文定义 Redis 实时数据库的物理存储约定。业务统一通过 [公共实时数据访问接口](access-design.md) 访问共享内存、Redis 或其他内存数据库；作用域和资产身份跨后端保持一致，Redis 命令与键格式留在 Redis 适配器内。配置权威数据保存在 MySQL，消息走 ZeroMQ。

采用“全局唯一储能单元 code → 设备 code → 子设备 code → 更深层设备 code”的路径设计。它与资产树一致，能直接定位单元、设备和部件，也避免让业务地址依赖采集进程的分片编号。

单元作用域由 `{<unit_code>}` 标识；测点键统一以 `ems:rt:point:` 开头，再接作用域、设备层级和测点 code，例如 `ems:rt:point:{ess000001}:pcs01:active_power`。`<...>` 表示占位符，实际键只保留 code 外的一对花括号。站统一使用 `station`、`station_id`、`station_code`，API 集合名为 `stations`。

| 设计点 | 约定及理由 |
| --- | --- |
| 单元根 | unit_code 全局唯一，站的 code 不重复放进单元键；station 归属放在元数据和消息中 |
| 设备层级 | 设备 code 以冒号直接串联为 `<设备code>:<子设备code>:<更深层code>`；不固定为三层 |
| code 与 ID | code 构成可读地址，稳定 ID 继续作为数据库关联、历史和审计身份 |
| 测点类型前缀 | 测点使用 `ems:rt:point:<作用域>:<设备层级>:<point_code>`；单元或 station 自身测点省略设备层级 |
| 其他资源 | 元数据、状态等继续使用资产路径后的 `meta`、`rt:state` 等资源后缀 |
| 遥测主题 | ZeroMQ 按单元或 station 作用域区分，事件载荷携带设备路径；不创建 Redis Stream |
| 公共资源 | station 公共设备、共享通道、站级策略拥有独立 station 根，不挂到任意单元下面 |

每一级仅保留 code，节点种类由元数据 `asset_type` 区分。Rack、Pack、Cell 等没有协议连接的部件也可占用设备路径节点；路径表示资产包含关系，通信、供电和管理关系另行建模。

## 2. code 唯一性与身份

### 2.1 格式与约束

| 对象 | 唯一范围 | 持久约束 |
| --- | --- | --- |
| station_code | 整个 EMS 系统 | `station.code` 非空，唯一索引 |
| unit_code | 整个 EMS 系统，跨 station 唯一 | `ess_unit.code` 非空，唯一索引；不能仅约束 `(station_id, code)` |
| 设备及各层子设备 code | 同一个直接父资产节点下 | `asset_node(parent_asset_id, code)` 唯一索引；设备节点 parent_asset_id 必须非空 |
| point_code | 同一个所属资产节点下 | `point(asset_id, code)` 唯一索引，原始点与派生点不得重名 |
| channel_code / resource_code | 同一 station 内 | 各自的 `(station_id, code)` 唯一索引 |

本版 code 采用 `^[a-z0-9][a-z0-9_-]{0,63}$`，只接受小写 ASCII；禁止空白、冒号、花括号、斜杠及通配符。显示名称独立存放，可使用中文和自由格式。新建接口遇到不合规 code 直接拒绝，不静默转小写；已有编码先生成规范化映射并检查冲突再导入。

数字编号直接接在名称后面，例如 `ess000001`、`station001`、`pcs01`、`rack01`、`rs48501`；名称与编号之间不加连字符。code 校验在上述字符规则外拒绝 `-[0-9]` 片段；名称内部的单词连接仍可保留连字符，例如 `pcc-meter01`。编号位数由对应编码规则确定，code 按完整字符串处理，不能将数字部分转为整数后丢失前导零；已有 code 的调整仍按迁移流程执行。

设备及各层子设备 code 额外禁止使用保留字 `meta`、`rt`、`config`、`stream`、`command`、`channel`、`resource`、`process`，用于区分设备路径与资源后缀。该限制只针对设备路径节点，不额外限制 unit_code、station_code 或 point_code。各进程的创建校验、导入与 KeyBuilder 共用保留字列表；新增保留字前检查已有编码兼容性。

各 C++ 进程使用相同格式校验与数据库字符比较约定；数据库唯一约束负责解决并发创建竞态，不能仅靠“先查询是否存在”或 Redis 锁。绑定到同一资产的 station/ess_unit 扩展表 code 与资产节点 code 在同事务中保持一致，设备 code 直接取资产节点，不在 device 表独立维护另一份名称。

全局唯一需要统一编码登记机制。共用 MySQL 时由全局唯一索引约束；多站独立 MySQL、离线建站时必须预分配不重叠的编码范围或先由中心登记，不能各站都从 `ess001` 独立编号后声称全局唯一。导入和跨站汇总仍检查冲突，冲突时拒绝覆盖已有单元。已退役 code 保留登记、不复用给新资产。

### 2.2 稳定性与层级

- code 发布后作为稳定业务编码，普通改显示名称不改变键。改 code、移动父节点均走第 8 节迁移流程。
- 设备 code 不要求全局唯一：不同单元可以都有 `pcs01`，同一单元不同电池簇可以都有 `pack01`；完整祖先路径必须唯一。
- 每个节点只有一个主要父节点，校验无环、station 一致；共享设备只保存一份权威状态，通过关系引用，不复制挂接。
- station_id、unit_id、asset_id、device_id（如节点可通信）、point_id 保持稳定。键变化不更换历史身份；事件保留产生时的 code 路径与 config_version。
- 本版建议最大设备深度 8、键总长度不超过 2048 字节，由各 C++ 进程共用契约校验；这些是项目限制，调整需两端同步。

## 3. 路径语法与例子

下列 `U`、`S`、`D` 只是文档简写，不是实际键中的字符。

```text
U = ems:{<unit_code>}
S = ems:{station:<station_code>}
D = U 或 S 后追加一至多组 :<code>

测点键语法（方括号表示可选设备层级，不写入实际键）：
ems:rt:point:{<unit_code>}[:<设备code>:<子设备code>:...]:<point_code>
ems:rt:point:{station:<station_code>}[:<设备code>:<子设备code>:...]:<point_code>

单元级测点：
ems:rt:point:{ess000001}:soc

单元状态：
ems:{ess000001}:rt:state

设备测点：
ems:rt:point:{ess000001}:pcs01:active_power

子设备测点：
ems:rt:point:{ess000001}:battery01:rack01:soc

子子设备测点：
ems:rt:point:{ess000001}:battery01:rack01:pack01:voltage

更深部件测点：
ems:rt:point:{ess000001}:battery01:rack01:pack01:cell01:voltage

station 公共设备：
ems:rt:point:{station:station001}:pcc-meter01:active_power

station 共享通道：
ems:{station:station001}:channel:rs48501:rt:state
```

测点键先识别固定前缀 `ems:rt:point:`，再读取完整花括号作用域；其后最后一段是 point_code，中间零至多段是设备 code 路径。零段设备 code 表示单元或 station 自身测点，例如 `ems:rt:point:{station:station001}:active_power`。层级与测点归属由已发布配置校验，不根据 code 中的 `pcs`、`rack` 等文本猜测设备类型。其他资源键仍按 U/S/D 加资源后缀解析；设备保留字继续用于这些资源键。station tag 含 `station:`，而 unit_code 禁止冒号，两类作用域不会因 code 相同而重名。

## 4. 键目录与所有权

### 4.1 资产、实时值与索引

表中 `R` 表示 U 或 S，`N` 表示 R 或 D；`A` 是 N 去掉开头 `ems:` 后的资产路径，保留完整 hash tag。例如 N 为 `ems:{ess000001}:pcs01`，A 为 `{ess000001}:pcs01`，单元自身的 A 为 `{ess000001}`。

| 键模板 | 类型 | 内容与写入方 |
| --- | --- | --- |
| `N:rt:state` | Hash | 单元/station 聚合由策略写，设备通信与运行状态由指定状态所有者写；每个 Hash 明确唯一写角色 |
| `ems:rt:point:A:<point_code>` | Hash | A 按上述定义展开；一个测点完整最新样本，采集写原始点，策略写独立派生点 |
| `S:channel:<channel_code>:rt:state` | Hash | 通道/监听就绪、会话数与汇总状态、收发/失败时间、owner_epoch、应用版本；所属通信实例写 |
| `S:resource:<resource_code>:owner` | String（JSON） | 物理资源 owner_id、owner_epoch、租约身份；指定所有权协调器维护，带租约 TTL |
| `S:process:<role>:<instance_id>:heartbeat` | String（JSON） | 本实例心跳、启动代次、时间；各进程自行更新并设 TTL |

instance_id、资源 ID、命令 ID 等动态后缀统一使用 `^[a-z0-9][a-z0-9_-]{0,63}$`，role 来自固定枚举，owner_shard 使用同样的安全标识格式；由 KeyBuilder 校验，不拼接用户任意输入。配置版本与资产目录由 MySQL 查询，不能凭 Redis 键推断完整配置。

树形配置查询通过 MySQL 的版本化目录完成。Redis 键本身不是配置目录，前缀不会自动产生父子索引。业务请求不使用 `KEYS` 或全库 `SCAN` 拼资产树；运维遍历允许限速 SCAN，处理重复项，不能把遍历视为一致性快照。见 [Redis SCAN 说明](https://redis.io/docs/latest/commands/scan/)。

station 通道只记录一份权威通道汇总状态；服务端的一条通道可以有多个运行 session，不能用一个 connected 布尔值代替全部会话状态。首期 session 明细由有界运行诊断接口提供，不临时发明未版本化的 Redis 键目录。归属不同单元的测点仍各自使用 `ems:rt:point:{<unit_code>}:...`，上送映射只引用标准点，不复制权威采样。设备/通道/profile/会话的关系见 [设备、通道与协议](../../architecture/channel-protocol.md)。

通道更新与单元测点写入不要求跨槽原子性。多个单元引用同一站级电表时读取同一个 station 测点，通过关系模型避免重复计量。资源租约只辅助协调，设备执行前的旧主隔离要求仍按整体架构第 6.3 节执行。

### 4.2 配置

Redis 不保存配置快照、active/applied 版本或配置事件。各 C++ 进程通过公共 ormpp API 从 MySQL 读取指定版本；配置发布和实例应用状态也保存在 MySQL。ZeroMQ 只提示实例重读，提示遗漏后按 MySQL 版本定期对账。各实例的本地配置缓存必须按版本整体替换，不能混用不同版本的点位与路由。

### 4.3 事件与控制

Redis 不作为消息传输或命令执行入口。原始遥测经独立 ZeroMQ PUB/SUB 送存储进程，允许丢样；命令与告警的业务事实由 MySQL/outbox 保存，命令通过 ZeroMQ REQ/REP 交接，变化提示通过 PUB/SUB 发出。一个采集分片可服务多个单元，其命令端点按 `owner_shard` 路由；扩缩容后仍需校验 `owner_epoch`、有效期与设备所有权。

## 5. 实时数据字段与更新

统一接口管理的实时 Hash 增加内部 `_store_revision`，用于完整记录 CAS；`_store_` 前缀不接受业务任意写入。测点防护字段增加 `accepted_source_id、accepted_source_epoch、accepted_config_version、write_enabled`，由授权协调职责安装；普通采样在同一记录版本上保留并校验这些字段。revision 与业务 source_epoch/sequence 分别承担存储并发和来源顺序控制，详细语义见 [公共实时数据访问接口设计第 4、5 节](access-design.md)。

Redis 派生类采用同槽临时 Hash 构建完整替换数据，再经脚本检查版本并 RENAME 发布，临时键为 `R:_store_tmp:<write_id>`；write_id 使用唯一安全标识，键只供适配器使用，不进入资产查询。构建时设置短 TTL，发布前取消临时 TTL 以保持最新值默认不过期；取消后未发布的孤儿由管理清理器在确认超过操作预算及恢复保留期后限速回收，正常成功或版本冲突路径立即清理不再使用的临时键。授权和清理必须覆盖该前缀，不能误清当前在途写入。

单测点 Hash 至少包含 `point_id、value_type、value、unit、quality、source_time、ingest_time、source_epoch、sequence、config_version、event_id`。Redis Hash 字段采用统一字符串编码；value 使用 JSON 标量文本，value_type 说明业务类型，无效值编码为 JSON `null`，精确整数按契约使用十进制字符串。时间统一 UTC 并约定精度，不能让各进程及前端各用不同的时间单位。

同一完整样本中的值、质量、时间、代次和序号必须一起更新，不能让读者看到一半新、一半旧。写入方根据已批准的来源代次及配置版本校验所有权；同一代次比较 sequence，重复忽略，旧事件只补历史。来源切换必须先更新权威来源代次，不能直接按随机进程 ID 或不同来源的 sequence 排序。

采样进入有界内存队列，再分别更新 Redis 最新值和通过 ZeroMQ 发布原始事件，不设置本地文件缓冲。实时更新可以使用受限脚本保证单条完整记录的条件替换；遥测发布与实时写入不是同一事务。发布端、网络或存储端故障可能丢样，不能用 Redis 最新值补造历史。具体恢复和 outbox 规则沿用整体架构第 5.3 节。

## 6. Cluster 与容量边界

花括号只包住 unit_code，设备 code 放在其后。测点的 `rt:point:` 位于花括号之前；同单元实时状态键因此位于同一 hash slot，层级更深不会自动分散到其他节点。同槽多键限制与 hash tag 规则见 [Redis Cluster 规范](https://redis.io/docs/latest/operate/oss_and_stack/reference/cluster-spec/)。

工程上的代价是一个大单元的热点不能靠增加 Cluster 节点自动拆开；不同单元还可能散列到同一槽，不能理解为一个单元独占一槽或一个节点。station 公共根同样需要容量限制。验收按单元/站级根分别统计键数量、内存、实时更新率和脚本时延。

跨单元、单元与 station 之间分别读写，通过 config_version、采样时间和业务校验组合结果。pipeline 可减少往返，但不提供跨根事务快照。若最大单元的负载超过单槽预算，再设计明确版本的单元内分片方案，连同实时状态迁移一起调整，变更必须更新键契约版本。

## 7. 生命周期与恢复

| 数据 | 保留规则 |
| --- | --- |
| 最新测点与设备状态 | 默认保留最后值，用质量、采样时间和 stale_after 判陈旧；不因离线直接删除最后值 |
| 心跳与租约 | 按心跳间隔/容错预算配置 TTL；租约续期和释放必须核对持有者身份 |
| 命令状态投影 | 可按查询窗口过期，权威状态和幂等记录始终以 MySQL 为准 |
| 退役设备路径 | 经版本切换、消费确认和保留期后按清单清理；不复用旧 code 接收新资产数据 |

Redis 缺失或故障后从 MySQL 配置与业务记录、重新采集的设备状态恢复实时投影；不能用恢复后的最新值补造历史。实时容量不足触发显式告警和背压。保留期、TTL、容量和最长中断目标在部署配置中量化，不在键中编码。

## 8. 实施与迁移

当前为文档变更，不对运行中的 Redis 执行改名、删除或重放。后续按以下顺序实施：

1. 盘点旧键和旧命名字段、实际消费组及积压；建立稳定 ID → station/unit code → device_path 映射，修复重名、非法 code 和设备保留字冲突，落实统一数据库迁移和唯一索引。已有带设备类型前缀或测点类型后缀的键按新语法重建并核对，不能仅修改读取端的拼接方式。
2. 在 `contracts/` 维护键语法、字段和样例；C++ 公共 Redis 模块实现统一 KeyBuilder，各进程复用，共享契约样例。接口接收已解析的根、code 数组和资源类型，业务代码不手工拼键，访问前按 station/资产授权。
3. 站表、字段、JSON、Protobuf 映射、API、SQL 示例和日志统一采用 station 命名；若已有旧客户端，提供明确的兼容窗口和契约版本，保留已有 Protobuf 字段编号，不直接破坏持久表或历史标签。TDengine 已有历史表不因文档改名就自动迁移。
4. 从同一 MySQL 配置版本重建实时最新值投影，并校验 ID、点数、质量和时间；影子写入只用于验证，新旧路径只允许一套权威控制执行消费者。
5. 选定切换水位，切换生产者、查询和订阅配置；迁移前对旧 Stream 未消费记录和 PEL 做一次性对账，再停用旧消息入口。跨新旧键/槽操作分步执行，验证后再清理。
6. code 改名或移动设备时列出整棵子树受影响路径和遥测主题，暂停受影响的新控制，核对在途命令，按上述版本/水位切换。拒绝旧版本控制与旧路径实时写入；已收到的迟到旧事件仍按稳定 ID 归档，不恢复旧路径为权威值。回退也要先核对命令执行记录，避免重复设备动作。

## 9. 验收要求

- 不同 station 并发创建同一 unit_code 必须失败；离线导入重名、同父节点重名、非法分隔符、编号前连字符、设备保留字和大小写冲突都被拒绝；编号前导零在读写和键生成时保持一致。
- 不同单元同名设备、不同父节点同名子设备不冲突；支持多层路径，拒绝环、超深层级和越权访问。
- 各 C++ 进程对同一输入生成一致的实时键和字段；前端只使用逻辑身份与 JSON 字段，不生成物理 Redis 键。单元级、各层设备级和 station 测点都以 `ems:rt:point:` 开头，解析后的作用域、设备路径与 point_code 正确；station 与单元交互不依赖跨槽原子性。
- 共享通道和公共电表只有一份权威状态；按关系查到正确设备，跨单元策略不重复计量。
- 乱序、重复、来源切换、Redis 中断和存储重启后可对账实时状态；ZeroMQ 遥测丢样可观测，不能由 Redis 最新值推断缺失事件。
- 配置部分发布、改名、换父节点、单元迁移、旧命令迟到与回退不会导致重复执行或历史身份变化；观察到的应用版本与实例心跳一致。

官方规则核对日期为 2026-09-13；项目命名、深度、保留和迁移约定由本文定义，实际客户端版本与目标部署仍需集成验收。
