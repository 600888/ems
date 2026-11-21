# 遥控与遥调

采集进程执行已授权、已仲裁的 Modbus TCP/RTU、IEC104、IEC61850 MMS 单点命令。
其他进程统一调用 `rtdb::CommandClient`：先将独立命令记录写入实时库，再发布命令就绪提示。
共享内存部署直接写共享段；Redis 部署沿用同一 API，不经过采集侧消息请求。
采集侧订阅提示后从实时库读取并认领命令，设备读写仍由原连接工作线程独占。
策略仲裁和 Web 控制权限不由该低层入口代替。

```text
Web / 策略 / ems-command / 北向控制
  → CommandClient.submitCommand → 实时库 Pending（目标值、期限、config_revision）
  → command.pending 提示 → 采集侧读取实时库
  → MySQL ops_command 登记 Queued → 实时库 CAS 认领 Executing
  → MySQL 条件执行登记 → 协议下发 → MySQL 记录结果 → 实时库同步结果
```

每个 `command_id` 独立保存，不覆盖 YC/YX 实测值，也不会覆盖同一点的上一条命令。
`Pending` 只证明实时库写入成功；持久受理发生在采集侧写入 `ops_command` 后。
共享内存整体丢失时，尚未持久登记的 Pending 命令可能丢失，调用方不得将提交成功显示为设备成功。

## 构建与部署

构建启用 `EMS_WITH_ZEROMQ=ON`，Windows 预设默认开启。直接使用仓库内
`backend/third_party/libzmq` 源码，由当前工具链构建静态库；不下载或查找外部 ZeroMQ 安装包，
无需配置头文件与库路径。构建目标为 `ems-collector`、`ems-command` 和 `ems-web`。
不启用 ZeroMQ 时仍可通过实时库提交并由采集侧扫描执行，但配置的通知端点必须为空。
Windows 库支持 TCP/inproc，不提供 IPC，避免与 Web 网络库的 wepoll 符号冲突。
采集与 Web 配置的 `command_endpoint` 是通知端点，例如 `tcp://127.0.0.1:5558`。
采集侧 SUB 绑定，多个生产者的 PUB 连接该端点；它不再是 REQ/REP 命令受理端点。
生产者和执行者必须使用相同实时库部署、站级 `scope_code` 和通知端点。
端点为空时依靠扫描；未编译 ZeroMQ 却配置非空端点时明确拒绝打开命令客户端。

Web 遥控、遥调的提交与结果确认共用 3 秒期限，批量遥调不按点位串行累加等待时间。
前端将绝对期限传给 Web，采集端在发送前复核期限，超时未发送的命令记为 `Expired`；
已发送而未获确认的命令保留 `Unknown`，不能据此自动重发。反馈核验窗口最多 3 秒。
空反馈数组 `[]` 表示未配置反馈：收到设备协议确认后保留 `Acked`，页面明确显示
“协议已确认，实际状态尚未核验”，不会继续等待必然无法产生的反馈。

IEC104 默认收到地址、类型、值和阶段均匹配的正向激活确认（ACTCON）即记为协议确认，
保持连接，不强制等待结束确认（ACTTERM）。这兼容仅返回 ACTCON 的直接执行服务端。
确需等待 ACTTERM 时，在通道参数中显式配置 `command_require_termination: true`；
该模式下等待结束确认超时会明确区分“已收到激活确认”和“完全未收到确认”。

命令表为 `ops_command`，由现有 Python ORM 和 Alembic 初始基线统一维护，应用启动不建表。
开发库应按当前基线初始化；已有数据库需显式对齐该表后再开放命令入口。
本次没有增加迁移代际。它是运行记录，不存配置副本；所有配置有效性只比较站根
`cfg_device.config_revision`。修改配置并递增站根修订号后，原修订号的未发命令被拒绝。

业务服务依赖 `IDatabase` 和 `ICollectorCommandRepository`，不绑定关系数据库品牌。
公共 ORM 仓储按行锁能力装配：MySQL/PostgreSQL 使用 `ForUpdate`；SQLite 使用
`Transaction`，连接使用 WAL 模式，并以 `BEGIN IMMEDIATE` 和有界 busy timeout 串行化写事务。
当前正式 C++ 进程仍使用已有 MySQL 连接适配器；这不代表已提供 PostgreSQL/SQLite
正式连接工厂。Python 表模型及基线 DDL 对 SQLite/MySQL/PostgreSQL 做一致性验证。

每个 `scope_code` 只运行一个采集执行实例。非空通知端点绑定先于未决命令恢复。
共享内存仅供同机受信任进程使用；跨主机实时库部署需额外身份校验，不支持跨主机共享段。
通知不携带目标值，任何通知都只会触发实时库读取，不能凭消息载荷执行设备动作。

## 提交与查询

创建 `command.json`，将 `point_id`、站根修订号及 UTC 毫秒期限替换为当前实际值：

```json
{
  "op": "submit",
  "point_id": "pcs-active-power-setpoint",
  "value": 48.0,
  "config_revision": 7,
  "expires_at_ms": 1794700000000
}
```

期限最多为当前时间之后一小时；上面的时间只是字段示例。PowerShell 可用
`[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() + 5000` 生成五秒后的期限。
提交后保留原始文件、相同值及相同 ID；不能在重试时修改期限。

```powershell
ems-command conf/realtime.json station cmd_001 command.json tcp://127.0.0.1:5558
ems-command conf/realtime.json station cmd_001 --query
ems-command conf/realtime.json station cmd_001 --verify tcp://127.0.0.1:5558
```

命令 ID 为 1～128 字符的 ASCII 字母、数字、`-` 或 `_`。业务 JSON 最大 2048 字节，
拒绝未知字段、非有限数值、非整数期限或修订号。以稳定 `point_id` 解析路由，不能直接传协议地址。
只接受当前启用且有 `can_control` 的 Modbus/IEC104 客户端路由。

`CommandClient::submitCommand()` 返回写入结果、重复标记和本地通知入队标记。
同 ID 同载荷返回已有命令状态；同 ID 异载荷返回 `Conflict`。通知失败不撤销已写入命令，
也不能据此换 ID 重发。写入结果未知时用原 ID 查询实时库。
`readCommand()` 直接读取实时状态；`requestVerification()` 仅登记只读核验标记。
Web 的提交与查询同样使用这些接口，不再通过消息向采集侧请求结果。

通知主题为 `command.pending`，正文只有 `command_id`，编码沿用 `<topic> NUL <正文>`。
写入完成后才发布；执行结果直接查询实时库。通知是可丢弃提示，不承载执行指令。
采集主循环被通知唤醒后优先检查通知中的 ID，并每 100 毫秒执行有界轮转扫描，
启动时还会对账持久未决记录及实时库中的既有记录。晚订阅、断线和满通知队列不会使
已存命令依赖某条消息才能执行；过期命令只更新状态，不补发动作。

实时库命令记录占用配置的后端容量。当前保留记录供查询，不自动按时间清理；容量不足
明确拒绝新提交。维护时只能清理已持久归档的终结命令，并保留 MySQL 去重记录。
即使实时记录被清理，再次使用相同 ID 也要经持久记录核对，不能重放已经执行的动作。

## 调度和执行状态

每条独立连接有最多 128 个内存任务，每个调度点最多执行一个控制任务。
空闲等待可以被命令唤醒；Modbus 连续地址仍合并为一次有界读事务，在事务之间插队；
IEC104 按最多 64 个缓存点返回调度点。不会打断已经发送的帧，也不会并发操作协议句柄。
实时库按命令 ID 分页扫描，数据库待发记录按数据库 ID 分页扫描；满执行队列的命令
保留在待发记录中，超过期限后变为 `Expired`。认领使用实时库 revision 的条件写入，
重复通知及并发认领不会触发重复执行。持久执行登记失败或结果未知时不进行设备写入。
执行状态先持久保存，再写回实时库；同步失败加入待补同步集合，暂停本轮新调度，
恢复后读取持久事实补齐，进程重启也会修复既有实时记录中的迟到状态。

| 执行状态 | 含义与后续动作 |
| --- | --- |
| `Pending` | 已写实时库，等待采集侧登记；不表示设备已执行或持久受理 |
| `Queued` | 等待路由及工作线程；物理发送前复核期限、修订号并提交条件执行登记 |
| `Executing` | 发送意图已持久提交；可能正在设备 I/O 中 |
| `Acked` | 插件返回协议确认；未配置反馈时停留于此，不宣称设备状态已核验 |
| `Succeeded` | 配置的全部必需反馈判据由发送后的新样本满足 |
| `Rejected` / `Expired` | 发送前拒绝、到期，或设备明确返回协议拒绝；保留具体原因 |
| `Unknown` | 设备动作可能已发生；只安排读反馈，绝不自动重写 |

IEC61850 的 SP 写入和 CO 选择/执行/增强终止确认共用网页传入的绝对期限，最多 3 秒。
只有明确配置的写点、`allow_writes=true` 和绑定 `can_control` 同时满足才允许下发。
明确拒绝或发送前校验失败返回 `Rejected`；发送后超时、断线或不可信响应返回 `Unknown`，
关闭本次 association 以隔离迟到确认。正常确认保持连接，实际到位由配置反馈判定。

持久提交失败或提交回复丢失时不开始新动作。旧插件写接口只有 bool，写失败无法证明
未发送时保守记录 `Unknown`。重启将旧 `Executing` 转成 `Unknown`；新鲜度边界重新设为
恢复时刻，避免旧反馈误判。重复提交、显式 `verify` 及自动核验均不重发原动作。
停止及热加载收尾队列内未发任务；已进入设备 I/O 的任务等待有界协议调用返回再收敛。

遥调使用 `(工程值-offset)/scale`，先验证工程量程和原码类型范围，整数不静默截断。
遥控支持固定值或直接值。最小间隔在当前连接内按点/写组限制；同连接写入天然互斥。
脉冲复位策略尚未实现，`pulse_width_ms != 0` 明确拒绝，防止只发置位而不复位。

## 北向服务端控制

IEC104 以 `server` 角色监听时，上级主站的选择/执行不再一律负确认。通道 settings 的 `points` 中
带 `point_id` 的控制 ASDU 是北向控制点；选择只登记窗口（`select_timeout_ms`，默认 30 秒），
执行才产生本地请求。

```text
上级 SELECT/EXECUTE → 插件受理(ACT_CON) → 通道每周期 take_controls
  → CommandClient 写实时库 Pending → CommandService 持久登记/认领 → 路由 → 南向写入
  → 反馈核验 → resolve_control → 上级 ACT_TERM 正/负
```

- 与 `ems-command` 共用同一条持久受理、修订号校验和路由，不另开旁路；北向地址与南向点位
  通过 `point_id` 解耦，不要求两侧地址相同。
- 插件只在 `ACT_CON` 之后交付一次请求；宿主未在 `command_timeout_ms` 内回执，插件自行回负
  `ACT_TERM`，避免上级无限等待。
- 未选择直接执行（`require_select=true`）、选择过期、地址未配置或队列满，均回负 `ACT_CON`，
  不会写入命令表。
- 上送通道仍需由绑定建立；控制能力在通道运行后自动生效，不额外新增协议入口。
- 北向控制同样先写实时库，再进入统一认领与结果同步链路；通知端点为空时由扫描接续。

客户端方向的命令期限由 `ems_plugin_write_until` 传入，取 `min(调用方期限, command_timeout_ms)`；
期限已过时不发帧、不关闭会话。此前 ABI v1 只导出 `write`，Web 期限无法传到底层，现已补齐。

## 反馈核验与当前边界

反馈配置沿用 YK/YT 的 `feedback_json`，例如：

```json
[
  {
    "point_code": "active_power",
    "criterion": "within_tolerance",
    "tolerance": 0.5,
    "required": true
  }
]
```

`point_code` 指向同一归属设备、同一物理连接上的 YC/YX，支持 `equals` 和
`within_tolerance`；缺少 `expected` 时比较本次命令的工程值。YX 按取反配置转换。
只比较品质有效、接收时间严格晚于发送的样本；IEC104 单独返回本机接收时标，
设备自带时间不能代替新样本证明。

自动核验在 `feedback_timeout_ms` 内进行；协议确认后反馈仍未满足会转成 `Unknown`。
超过自动核验窗口可用 `--verify` 再读。配置变化后不使用新路由核验旧命令，
也不根据反馈不符盲目重试写入。当前跨物理连接反馈保留未决状态，不伪造成功。
跨连接/跨进程写组互锁、持久频率限制、控制权仲裁及 Web 业务控制接口仍须在业务层接入。
