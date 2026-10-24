# 设备管理设计与实时库、点表加载能力差距

排查日期：2026-10-24。依据当前打开的 `.pen/device_manage.pen`，通过 Pencil 读取全部 9 个顶层页面；核对当前工作区源码、调用链和已有测试。设计中的厂商、读数、版本和数量为示例，不作为必须逐项相等的业务数据。

结论：已有文件解析、事务入库、固有点目录、纯模型校验和统一实时存储基础，但“点表草稿 → 固有点映射 → 版本发布 → 采集应用 → 标准值写入 → 能力开放”尚未形成闭环。最大的缺口位于配置和运行模型的衔接，不宜仅按页面接口数量估算工作量。

本文的“未接通”指在本仓库当前生产源码中未发现对应实现或调用链；外部系统、未提交到本工作区的代码不在结论范围内。未修改业务代码或设计图。

**当前已有的能力**

| 能力 | 已核实的实现 | 实际边界 |
| --- | --- | --- |
| 四遥 XLSX 解析与入库 | `parse_point_table_excel()`、`import_configuration_points()`；支持 Modbus RTU/TCP、IEC104、DLT645 遥测，错误定位到工作表/行/列，反馈编码解析为 ID，整表事务提交与回滚 | 写现有工程配置表，不等于保存版本化设备草稿或发布运行配置 |
| 固有点映射草稿 | Modbus CSV/英文列 XLSX；Append/Replace；标准单位、路由权限、稳定厂家键校验；失败不部分应用 | 返回内存目录与草稿；未持久化、未交给采集执行 |
| 固有点和静态能力 | PCS/BMS/消防内置目录；未配置/启用/禁用三态；禁用原因；发布前覆盖性和权威路由检查 | 纯模型校验和静态依赖分析，不包含实时质量、授权或执行验收 |
| 实时存储基础 | 统一 DLL、共享内存/Redis 后端、记录读/CAS/条件删除/逐项批量、原四遥与拓扑兼容门面 | 存储能保存记录，不会自动执行标准投影、版本应用、来源授权或新鲜度判定 |
| 采集基础 | ParaLoader 从配置仓储读取；协议插件/Handler；通道线程；DataDispatcher 写统一实时库 | 当前使用旧四遥记录及 code 采集，尚未消费公共模型的 VendorPointRoute/DevicePointDraft |

两种点表接口不能混为同一个导入流程：

```text
四遥中文 XLSX
  → parse_point_table_excel / import_configuration_points
  → 设备、通道、四遥工程表
  → ParaLoader → CollectEngine → ChannelThread → DataDispatcher → RtDb

Modbus CSV / 英文列 XLSX
  → import_point_table_file → load_point_table
  → ModelCatalog + DevicePointDraft + vendor_rows（内存候选）
  → 尚缺持久化、发布及采集应用适配
```

**需要优先修复的具体实现问题**

1. **采集丢失点表中的协议参数。** `CollectEngine::build_device_contexts()` 只把 `pt.code` 放进测点列表；`ChannelThread::start()` 初始化 Handler 时也没有注入点表的完整测点定义。`collect_points()` 直接将这些 code 传给插件。Modbus 插件按 `slave:function:offset[:decode[:endian[:bit]]]` 解析编码，而四遥 XLSX 允许 `PCS_DC_V` 这样的业务编码，地址/解析码/倍率另列保存。因此，当前链路不能据入库记录自动采集这些业务编码；即使用协议串充当 code，单独保存的倍率、偏移等也没有在该链路应用。
   - 证据：[上下文构造](../../backend/src/net/collect/collect_engine.cpp#L330)、[Handler 初始化](../../backend/src/net/collect/channel_thread.cpp#L59)、[批量读取](../../backend/src/net/collect/channel_thread.cpp#L337)、[Modbus 编码解析](../../backend/src/net/protocol/modbus/modbus_protocol.cpp#L89)。
   - 补齐：把业务身份、协议地址和转换规则分开；由已应用配置生成完整采集计划，采集结果投影回稳定 point_id。

2. **实时库配置加载接口返回了虚假成功。** `loadYcConfigsFromDb()`、`loadYxConfigsFromDb()`、`loadYkConfigsFromDb()`、`loadYtConfigsFromDb()` 均直接 `return true`，没有数据库访问；`loadAllConfigs()` 因此可返回成功而缓存为空。JSON 加载只实际解析 `yc_points`，另外三类数组未解析；CSV 加载直接返回 false。独立的公共 CSV/XLSX 导入器已有实现，不能据此说整个项目没有 CSV 导入。
   - 证据：[数据库加载空实现](../../backend/src/rtdb/client/rtdb_manager.cpp#L35)、[JSON 加载](../../backend/src/rtdb/client/rtdb_manager.cpp#L418)、[CSV 占位](../../backend/src/rtdb/client/rtdb_manager.cpp#L525)。
   - 影响：依赖配置缓存枚举的完整点位查询可能为空，名称、单位、限值等配置无法自动补全。
   - 补齐：统一配置入口，删除或明确拒绝未实现入口，按精确运行版本整体构建缓存。

3. **状态点类型依赖编码前缀猜测。** `batch_update()` 仅将 `YX_`/`yx_` 开头的 code 分入遥信，其他全部默认遥测。引擎即使从遥信列表读出 `PCS_RUN` 或设计中的 `run_state`，仍丢失了明确的点种类，最终可能写入 YC 集合。
   - 证据：[批量分发](../../backend/src/net/collect/data_dispatcher.cpp#L272)、[遥信读取后共用分发入口](../../backend/src/net/collect/channel_thread.cpp#L327)。
   - 补齐：采样携带声明的 PointKind/ValueType，禁止从业务名称推断类型。

4. **禁用测点、禁用设备没有在采集计划中形成完整过滤。** `load_yc_points()`/`load_yx_points()` 等方法只按通道筛选，没有筛选 `point.enable`。`load_channels()` 过滤通道自身启用状态，但不核验所属设备是否启用；`start_all()` 直接加载通道和测点，`build_device_contexts()` 不再核验点/设备启用状态。因此，在通道仍启用时，禁用点或已禁用设备的点仍可能进入轮询。
   - 证据：[通道筛选](../../backend/src/net/para_loader/para_loader.cpp#L104)、[测点筛选](../../backend/src/net/para_loader/para_loader.cpp#L143)、[启动入口](../../backend/src/net/collect/collect_engine.cpp#L83)、[上下文构造](../../backend/src/net/collect/collect_engine.cpp#L330)。
   - 补齐：从一致版本的配置统一编译启用集合；配置禁用与运行离线分开维护。

**对照设计仍缺的业务能力**

| 编号 | 设计要求 | 当前差距 | 优先级 |
| --- | --- | --- | --- |
| G1 | 第 04–06 页：保存草稿、差异预览、不可变发布、采集/策略应用确认 | 有快照/配置提供者抽象，未发现其落地类或完整发布服务；ParaLoader 读取可变工程表，不读取带 activation revision 的精确版本；缺草稿/运行隔离、差异、应用回执、失败状态和新版本回滚编排 | 最高 |
| G2 | 第 02/05/09 页：运行监视、控制能力随质量与版本一致性开放 | `resolve_standard_capability()` 只看固有点静态状态；无运行门面综合新鲜度、来源授权、配置一致性和权限作判断 | 最高 |
| G3 | 第 01/08 页：BMS 采集到 Battery/Rack/Pack/Cell 节点 | 公共资产树能表达部件，但导入行无目标 asset/point 字段；固有点校验强制 `point.asset_id == device.asset_id`，不能让 bms01 的这套固有点草稿映射到其子资产点 | 最高 |
| G4 | 第 05 页：状态枚举、事件、固定命令值、写入转换和反馈判据 | 映射加载仅支持 bool→Bool、数值→Double；PCS 的 run_state 为 String，不能把 u16 状态字转换成枚举字符串。缺版本化枚举表、未知原码保留、事件映射、start=1/stop=2 固定写值、共享寄存器互斥和反馈判据配置 | 最高 |
| G5 | 第 01/02/08 页：标准点稳定身份、路径和来源诊断 | 记录层已有 scope/device_path；采集使用的兼容门面仍以四遥类别+code 为逻辑键，无自动 point_id/资产路径解析；旧运行时载荷不带公共 PointSample 中的 source_epoch/sequence/config_version，缺旧来源与旧版本样本拒绝 | 最高 |
| G6 | 第 03 页：共享连接、多绑定、独立采集与控制角色、协议 Profile | 公共模型有多对多绑定和资源引用，现有 ChannelRecord 却把单一 device_id 和协议地址放在通道上，采集按通道创建独立 Handler；缺绑定仓储、资源会话复用和由绑定生成运行计划的适配 | 高 |
| G7 | 第 06 页：厂家扩展点保留采集，仅做诊断展示 | `standard_code` 为空时加载器跳过路由，只把原始行留在返回值 `vendor_rows`；缺持久化、扩展点采集计划和诊断查询 | 高 |
| G8 | 第 04 页：地址重复/区间冲突检查和逐项确认修正 | 已有稳定键、标准来源、范围、字节序和单位检查，但无按绑定/地址空间/访问方向/位段进行寄存器占用区间检查；缺可保存的确认项、行级修正与重新校验。不能把所有共享地址一概当错误，start/stop 共享写寄存器应显式声明 | 高 |
| G9 | 第 01/03/08 页：在线/离线/告警/故障、最近有效数据、5 分钟通信统计、层级有效率 | 通道线程只有状态/错误等基础信息，分发器统计为写入/通知计数；缺按设备/绑定的最近有效通信、响应时延/成功率窗口、质量与告警聚合、Pack/Cell 极值及位置的查询投影 | 高 |
| G10 | 第 07 页：设备档案和复用型号模板 | 公共 Device 有厂家/型号版本/固件字段，但实际 DeviceRecord 没有这些字段和 parent_asset_id；内置固有点目录仅 PCS/BMS/消防，未见空调/环控/电表/保护的对应固有点目录。缺模板版本仓储、实例化、同父编码检查与设备档案到公共模型的转换 | 中 |
| G11 | 第 04/06 页：导入来源追踪、重复导入、稳定 ID 与历史语义 | 公共草稿替换可保留标准 point_id/路由 ID；四遥数据库 Replace 则删后重建并分配新数据库 ID。缺两类身份的持久绑定、导入任务 ID/文件 SHA-256/基础版本、重复提交幂等、操作人与原因审计 | 高 |
| G12 | 跨协议设备沿同一流程导入并投运 | 四遥 Excel 可处理 Modbus/IEC104/DLT645 的相应范围，但标准映射导入仅 Modbus；已存在 DLT698 协议接入不代表点表导入已支持。缺统一协议点表适配和各协议到标准点的完整编译链 | 中 |

**关键差距的源码依据**

- G1：[配置快照写入接口](../../backend/include/common/config/i_config_snapshot_store.h#L25)、[精确版本读取接口](../../backend/include/common/config/i_config_provider.h#L27)、[当前事务入库服务](../../backend/src/common/db/configuration/import_service.cpp#L4)、[ParaLoader reload](../../backend/src/net/para_loader/para_loader.cpp#L34)。全仓生产源码搜索未找到 `IConfigProvider`/`IConfigSnapshotStore` 派生实现；策略目录目前是接口目标，不能把设计上的“策略已应用”当作已有服务回执。
- G2/G4：[静态能力解析](../../backend/src/common/model/device_config.cpp#L222)、[Bool/Double 映射限制](../../backend/src/common/config/point_table.cpp#L540)、[PCS 固有点种类](../../backend/src/common/model/device_models.cpp#L38)、[当前命令任务](../../backend/src/net/collect/channel_thread.h#L57)。现有四遥表的 related_yx_id/related_yc_id 是基础关联，不能代表“回显+实际功率”的多判据验收。
- G3：[点必须位于设备自身资产的限制](../../backend/src/common/model/device_config.cpp#L132)、[导入行字段](../../backend/include/common/config/point_table.h#L17)。不应为每个 Cell 虚构独立通信设备来绕过该限制。
- G5：[公共类型化采样](../../backend/include/common/model/point.h#L154)、[实际 YC 运行载荷](../../backend/src/models/point_runtime.hpp#L95)、[兼容门面写点键](../../backend/src/rtdb/client/rtdb.cpp#L477)。CAS 解决存储并发，不能替代样本 source_epoch/sequence 的业务新旧判断。
- G6：[公共多对多绑定](../../backend/include/common/model/binding.h#L45)、[实际通道记录](../../backend/include/common/database/configuration/channel.h#L13)、[每通道创建 Handler](../../backend/src/net/collect/channel_thread.cpp#L51)。共享一个通道内的处理器不等于已有设计中的跨绑定共享资源生命周期。
- G7：[未映射厂家行跳过路由](../../backend/src/common/config/point_table.cpp#L514)。原始行留在函数结果里不等于会采集。
- G8：[当前路由去重键](../../backend/src/common/model/validation.cpp#L280)。该键包括 point_id，不能检查两个不同点占用同一寄存器区间；CSV/XLSX 解析也没有占用区间表。
- G9：[读取失败直接返回](../../backend/src/net/collect/channel_thread.cpp#L341)、[旧品质枚举仅四值](../../backend/src/models/base.h#L230)、[公共品质模型](../../backend/include/common/model/point.h#L38)。批量调用整体失败后旧值可继续留在实时库，需要读侧新鲜度和离线状态投影；不能仅依赖最后一次 Good 品质。
- G10：[实际设备档案字段](../../backend/include/common/database/configuration/device.h#L13)、[公共设备字段](../../backend/include/common/model/device.h#L24)、[内置模型目录](../../backend/include/common/model/device_models.h#L54)。group_id 不能直接替代可递归的资产父节点。
- G11：[四遥 Replace 删除](../../backend/src/common/db/configuration/point_table_store.hpp#L122)、[重新生成 ID](../../backend/src/common/db/configuration/point_table_store.hpp#L141)、[公共映射 Replace](../../backend/src/common/config/point_table.cpp#L499)。还存在粒度差别：公共 Replace 重建本设备草稿涉及的全部标准路由，四遥 Replace 仅替换目标设备+通道；多绑定场景必须在产品与接口层明确替换范围。
- G12：[四遥 Excel 协议范围](../../backend/src/common/db/configuration/point_table.cpp#L124)、[标准映射仅接收 Modbus](../../backend/src/common/config/point_table.cpp#L488)。

**进一步需要纳入实现的语义细节**

- 地址和字节序：设计示例显示十六进制地址和单寄存器 AB。公共 CSV 只接受十进制协议偏移，单寄存器字节序要求留空；四遥 XLSX 支持十六进制和 AB/BA，但缺省宽类型字节序可回落到默认顺序。两套入口需要统一显式约定和预览展示，不能静默把文档编号换算成协议偏移。
- 数据类型：Handler 的统一值是 double，DataDispatcher 重建 rawValue 并将数值转为 int64，未传递原始 details。公共模型已能表达可空值、64 位整数和字符串，但尚未接入这条实时链路。大整数、原始值、小数和复合协议诊断有精度或信息丢失风险。
- 配置一致性：四类缓存各自清空再加载，没有整份配置版本和整体切换接口；不宜用依次调用四个 load 方法模拟一次版本应用。
- 权限与审计：设计的只读账号、发布原因和操作者需要 Web/发布服务承接。实时库保存一个 owner 字符串或获取业务锁不等于完成控制授权。
- 整批语义：实时库 writeMany 是逐记录提交，不保证设备整批快照原子性。页面读取多个诊断值时需携带版本/采样信息并明确一致性策略；无需为所有页面强行增加跨记录事务。

**建议实施顺序与验收条件**

1. 先统一“稳定业务点身份 → 版本化路由 → 协议请求 → 标准值”的采集计划；修复参数未传递、类型猜测、禁用项过滤和加载接口虚假成功。验收：业务编码 `PCS_DC_V` 可按点表地址读取，非 1 倍率正确，`PCS_RUN` 入状态集合，禁用项不轮询。
2. 落地草稿/快照/发布服务及精确版本应用。验收：保存 v13 草稿后运行仍是 v12；发布后可见采集/策略各自的应用回执；版本不一致时控制能力关闭；失败可追踪，回滚创建新版本。
3. 完成目标资产映射、枚举转换、命令固定值/逆向编码/互斥/反馈规则和厂家扩展点采集。验收：BMS 来源明确写入指定 Pack/Cell；u16 状态可转标准枚举并保留未知原码；start/stop 可共用寄存器且分别写入 1/2；扩展点可查原始诊断。
4. 完成类型化实时门面、样本来源/顺序检查、新鲜度、设备与通道状态投影。验收：旧来源或旧配置样本不会覆盖当前样本，离线/陈旧不会显示为有效当前值；设备能力随运行条件变化。
5. 补模板与档案仓储、导入追溯/幂等/审计、跨协议适配及页面查询接口。

应新增的关键集成测试包括：XLSX 业务编码到真实协议请求、倍率/端序实际生效、多设备相同 standard_code 隔离、无前缀状态点、设备/点禁用过滤、同地址写命令互斥、BMS 子资产路由、草稿不影响运行、精确版本热应用及回执、旧来源拒绝、扩展点持续采集、导入重试和身份稳定。

**本次验证**

复跑工作区已有 MSVC Release 构建产物，未重新编译：

- `test_ems_common.exe --gtest_filter=PointTableImport.*:PointTableLoad.*:DeviceConfig.*:DeviceModels.*`：20 项通过。
- `test_point_table_db.exe --gtest_filter=PointExcel.*:PointDatabase.*`：21 项通过，数据库场景使用测试自身的 SQLite 临时库。
- 结果：[公共模型与导入测试 XML](../../build/device-management-audit/common.xml)、[Excel 与配置仓储测试 XML](../../build/device-management-audit/database.xml)。临时文件隔离在 `build/device-management-audit/`。

41 项通过说明现有解析、纯模型校验和事务等已覆盖行为可运行；不能据此认定本文缺失的端到端发布、控制、真实设备通信或真实 MySQL/Redis 联调已验收。本次缺口结论以当前源码与设计对照为主。
