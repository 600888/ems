# 设备模块设计

> 日期：2026-10-24｜状态：首期实现，运行与限制见[构建运行](../running.md)。本文看业务图，[API 契约](api.md)看固定路由，[数据库设计](../../../common/database/device-management.md)看字段。

## 1. 模块职责

```mermaid
flowchart TB
    API[device/api 固定路由] --> App[device/application 用例服务]
    App --> Tree[树与档案]
    App --> Binding[通道与绑定]
    App --> Point[四遥与固有点]
    App --> Import[文件导入]
    App --> Catalog[类型与模板]
    Tree --> Writer[ConfigurationWriter 保存事务]
    Binding --> Writer
    Point --> Writer
    Catalog --> Writer
    Import -->|应用预览| Writer
    Writer --> Common[公共校验与 Repository]
    Import --> Parser[纯解析器适配]
    Common --> DB[(MySQL)]
```

| 既有基础 | 本次补齐 |
| --- | --- |
| 14 表行结构、ORM 映射 | Web 入口、服务、身份和 JSON DTO |
| IDeviceConfigRepository 基础 CRUD、整份读取 | 真正分页与 total、前置站根锁、模板/导入仓储 |
| 公共数据库工厂、调用方事务 | 14 表完整 schema 检查、跨表一致性校验 |
| CSV/XLSX 解析基础 | 新四遥表适配、逐行确认、稳定身份与原子应用 |

公共入口位于 `include/common/database/configuration/`；持久化实现只放 `src/common/db/configuration/`。旧资产、配置快照和草稿接口不接入设备 Web。

## 2. 一棵设备树

```mermaid
flowchart TB
    Station[station 站根：唯一根] --> Unit[unit 储能单元]
    Station --> Public[device 站级设备]
    Unit --> PCS[device PCS]
    Unit --> BMS[device BMS]
    BMS --> Battery[component 电池系统]
    Battery --> Rack[component 簇]
    Rack --> Pack[component 包]
    Pack --> Cell[component 电芯]
    BMS -.source_device_id.-> Rack
    BMS -.source_device_id.-> Pack
    BMS -.source_device_id.-> Cell
```

| 节点 | 父级 | 通道 / 点 | 限制 |
| --- | --- | --- | --- |
| station | 无 | 都不能 | 种子建立，不开放创建/删除；path=/，depth=0 |
| unit | station | 都不能 | 组织节点 |
| device | station / unit | 可绑定、可挂点 | 必须指定类型与 type_version |
| component | device / component | 不绑定、可挂点 | 来源必须为所属通信设备子树的实际采集设备 |

- ID 稳定，code 同父唯一；不按 ID 前缀猜类型。path/depth 后端生成，最多 8 层。
- 创建后不直接改 code、父级、node_kind 或类型；移动/迁移留给专门用例。
- 树按父节点懒加载；列表服务端分页，total 和节点统计均按授权范围计算。

## 3. 从创建到启用

```mermaid
flowchart LR
    Create[创建停用设备] --> Archive[档案直接入工程表]
    Archive --> Bind[配置真实绑定]
    Bind --> Points[导入或编辑四遥]
    Points --> Check{启用完整性通过}
    Check -->|否| Issues[返回逐项问题]
    Issues --> Points
    Check -->|是| Enable[启用并递增 revision]
    Enable --> Runtime[消费者轮询热加载]
```

| 操作 | 强制检查 |
| --- | --- |
| 任意保存 | 类型、唯一性、层级、来源、已存在引用正确 |
| 启用 / 修改已启用路径 | 必需固有点已映射或明确禁用；来源、转换、反馈和绑定完整 |
| 停用 | 保留工程记录；排除设备、祖先、来源、通道、绑定或点任一停用路径 |
| 删除 | 拒绝站根及任何子节点、点、绑定、来源、反馈、任务引用；不隐式级联 |

停用设备仍是当前工程配置，没有额外草稿。后台能力仅表示 configuration_ready，运行许可由监视/控制侧另行判定。

## 4. 通道、绑定与部件来源

```mermaid
flowchart LR
    D1[通信设备 A] --> B1[绑定 A：采集与控制角色]
    D2[通信设备 B] --> B2[绑定 B：独立设备地址]
    B1 --> Channel[同一 cfg_channel 物理连接]
    B2 --> Channel
    C[部件 owner_device_id] --> P[部件四遥点]
    P -->|binding_id| B1
    C -.source_device_id.-> D1
```

| 规则 | 行为 |
| --- | --- |
| 同设备 + 同通道 | 一条绑定，用 can_collect/control/forward 合并角色 |
| 复用连接 | 不自动复制通道；移除绑定不删除通道 |
| 新建连接并绑定 | 一个事务，失败全部撤销，只加一次 revision |
| 点来源 | YC/YX 要 can_collect；YK/YT 要 can_control |
| 共享通道修改 | 检查全部受影响资源权限并返回影响设备 |
| Profile | 仅用现有 profile_version 和真实能力目录，不新增配置发布流程 |

## 5. 四遥与固有点

```mermaid
flowchart LR
    Type[类型固有点目录] --> Match{查对应 semantic_code}
    Match -->|无行| Missing[unconfigured]
    Match -->|enabled=true| Enabled[enabled]
    Match -->|enabled=false| Disabled[disabled + 原因]
    Enabled --> Kind{point_type}
    Disabled --> Kind
    Kind --> YC[cfg_point_yc / YcPoint]
    Kind --> YX[cfg_point_yx / YxPoint：含 SOE]
    Kind --> YK[cfg_point_yk / YkPoint]
    Kind --> YT[cfg_point_yt / YtPoint]
```

| 检查 | 规则 |
| --- | --- |
| 分类 | DTO 的 point_type 白名单分派四表，不加单表分类列；运行 YK/YT 都是 Command |
| 唯一性 | point_id、节点 code、绑定 vendor_key 跨四表校验；固有点须匹配目录分类 |
| 扩展点 | semantic_code 为空也保留采集，不跳过 |
| 换算 | YC=raw×scale+offset；YT=(工程值-offset)/scale；非零倍率、范围和溢出检查 |
| 反馈 | 解析为同归属节点的稳定 YC/YX 点；检查类型、存在性及循环 |
| 地址 | 检查绑定内区间/位段及共享通道同设备地址；读读允许，读写重叠须确认 |
| 写冲突 | 仅允许明确同一非空写组且执行侧支持互斥的共享写；非法冲突不可确认绕过 |

### 当前模型限制

| 限制 | 首期处理 |
| --- | --- |
| 禁用点仍需真实 binding_id | 无绑定时只表示未配置，不造占位绑定 |
| byte_order 仅 ab/ba | 当前仅开放 bool/i16/u16；多寄存器布局返回 Unsupported，不截断 |
| rated_power_kw 可空性映射为 0 | 不从 0 推断缺失；需要区分时先改公共映射 |
| 导入行缺 YK 反馈等专有字段 | 只开放可无损持久化格式；其他返回 Unsupported |
| 设备只存 template_id | 不假造当初套用的 template_version |

扩展时原地修改 `003`、行结构、ORM 与数据库文档，不增加迁移代际或配置版本。

## 6. 模板实例化

```mermaid
flowchart LR
    Select[选择模板 ID 与业务版本] --> Preview[查询内容预览]
    Preview --> Binding[选择已存在真实绑定]
    Binding --> Merge[merge_missing：只补缺项]
    Merge --> Validate[检查类型与全部候选点]
    Validate --> Save[同事务写四遥与 revision]
```

模板不自动覆盖已有人工作业；无绑定时只保存模板来源。类型、模板、Profile 和固件业务版本保留。

## 7. 保存事务与热加载

```mermaid
sequenceDiagram
    participant U as 前端
    participant W as ConfigurationWriter
    participant D as MySQL
    participant C as 消费者
    U->>W: JSON：对象 ID、修改、expected_config_revision
    W->>D: BEGIN，先锁站根
    D-->>W: 当前 revision
    alt revision 不匹配
        W->>D: ROLLBACK
        W-->>U: 409，返回当前计数
    else 匹配
        W->>D: 读取当前关系、授权与候选校验
        W->>D: 写工程表；实际变化才加一次 revision
        W->>D: COMMIT
        W-->>U: 保存结果与 revision
        C->>D: 轮询 revision，一致事务读工程
        C->>C: 编译成功后原子切换
    end
```

| 情况 | 处理 |
| --- | --- |
| 任一步失败 | 整事务撤销，revision 不变 |
| 多行 / 模板 / 导入一次保存 | 只增加一次计数；无变化不增加 |
| 双窗口修改同一计数 | 先保存成功，后保存 409；保留本地输入后重读 |
| commit 返回 Unknown | 按原对象/任务查询，不盲目重放 |
| 消费者加载失败 | 保留旧运行目录并告警；缺真实诊断时显示 unknown |

锁站根必须先于读取前置条件和写入；所有工程写入者遵守同一顺序，不能只在末尾 bump。消费者不在线也可保存，不建立发布/回执表。

## 8. 文件导入

```mermaid
flowchart TB
    Upload[multipart：file + JSON metadata] --> Limit[文件、格式、解压与行数限额]
    Limit --> Parse[工作池纯解析；不持有数据库连接]
    Parse --> Key{同设备、绑定、文件摘要任务已存在}
    Key -->|是| Options{解析选项相同}
    Options -->|是| Replay[返回原任务]
    Options -->|否| Conflict[409]
    Key -->|否| Preview[持久任务与逐行预览]
    Preview --> Fix[修正 / 确认 / 排除行]
    Fix --> Hash[检查 JSON 中的 expected_preview_hash]
    Hash --> Validate[重校验未排除行]
    Validate --> Apply[先锁站根再锁任务]
    Apply --> Write[原子写四遥、resolved_point_id、applied 与 revision]
```

```mermaid
stateDiagram-v2
    [*] --> parsed: 解析并持久化
    [*] --> failed: 无法解析
    parsed --> parsed: 修正并重新计算预览哈希
    parsed --> confirmed: 未排除行全部通过或确认
    confirmed --> parsed: 修改使原确认失效
    confirmed --> applied: 事务应用成功
    applied --> applied: 重放只返回原状态
    applied --> [*]
    failed --> [*]
```

| 约束 | 规则 |
| --- | --- |
| 入参 | file 放原文件；metadata 放 device_id/binding_id/source_kind/address_base |
| 预览并发 | 动态 preview_hash，不加版本列；编辑/校验/应用都在 JSON 传期望哈希 |
| 应用并发 | 再核对 expected_config_revision；预览成功不代表可绕过保存校验 |
| 错误 | 未排除 error、未确认 warning、目标缺失阻止应用；rejected 明确计数 |
| 稳定点 | 按 binding_id+vendor_key 查四表，更新复用 point_id；目标按 path 精确命中 |
| 合并 | 仅 merge/upsert；文件缺席旧点保留，不默认删除 |
| 重放 | applied 不再写点、不再加计数；原提交 revision 未存则不假造 |
| 解析适配 | 复用纯解析，不调用旧表写入/删后重建 ID 的流程 |
| 格式能力 | 按实际解析/存储/运行能力交集开放；中文多表定位等缺持久字段时不开放 |

导入新点由规范列生成名称、地址、转换；code 优先 semantic_code，否则由 vendor_key 稳定生成并查碰撞。更新保留 point_id/code/enabled 及当前格式不表达的专有规则，禁止丢失命令语义。

## 9. 前端联调与验收

```mermaid
flowchart LR
    Mock[关闭 mock] --> Tree[树懒加载与真实分页]
    Tree --> Detail[按 JSON ID 读取档案]
    Detail --> Binding[合并绑定角色]
    Binding --> Points[YC / YX / YK / YT]
    Points --> Import[导入预览与确认]
    Import --> Save[保存后刷新]
```

| 前端调整 | 验收 |
| --- | --- |
| 发布页改校验页，移除草稿筛选和模拟计时 | 保存直接写工程；加载未知如实展示 |
| 四遥独立页签；event 改 YX+SOE 筛选 | 分类正确，YK/YT 不进周期采集 |
| total、ID、来源、能力来自接口 | 不按前缀猜类型、不写死数量 |
| 创建成功后再关闭抽屉 | 刷新仍存在；失败不报成功 |
| 按 device_id/任务隔离编辑上下文 | 切换设备无串数据；409 不覆盖本地输入 |
| 公共权限与事务测试 | 越权不可见、引用删除失败、整批回滚、重复导入不重复应用 |
