# 遥控与遥调

采集进程执行已授权、已仲裁的 Modbus TCP/RTU、IEC104 单点命令。跨进程使用独立
ZeroMQ REQ/REP，接收线程持久登记后立即回复；设备读写仍由原连接工作线程独占。
策略仲裁和 Web 控制权限不由该低层入口代替。

## 构建与部署

构建启用 `EMS_WITH_ZEROMQ=ON`，Windows 预设默认开启。直接使用仓库内
`backend/third_party/libzmq` 源码，由当前工具链构建静态库；不下载或查找外部 ZeroMQ 安装包，
无需配置头文件与库路径。构建目标为 `ems-collector`、`ems-command` 和 `ems-web`。
Windows 库支持 TCP/inproc，不提供 IPC，避免与 Web 网络库的 wepoll 符号冲突。
采集配置增加 `"command_endpoint": "tcp://127.0.0.1:5558"`；空串表示不开放命令服务。
未编译 ZeroMQ 却配置端点时明确拒绝启动。

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

每个 `scope_code` 只运行一个采集执行实例，并固定唯一命令端点。端点绑定先于未决命令恢复。
该入口面向可信本机调用方，未配置远程认证；跨主机接入需额外可信传输和身份校验。

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
ems-command tcp://127.0.0.1:5558 cmd_001 command.json
ems-command tcp://127.0.0.1:5558 cmd_001 --query
ems-command tcp://127.0.0.1:5558 cmd_001 --verify
```

命令 ID 为 1～128 字符的 ASCII 字母、数字、`-` 或 `_`。业务 JSON 最大 2048 字节，
拒绝未知字段、非有限数值、非整数期限或修订号。以稳定 `point_id` 解析路由，不能直接传协议地址。
只接受当前启用且有 `can_control` 的 Modbus/IEC104 客户端路由。

| 传输回复 | 含义 |
| --- | --- |
| `Accepted` | `Queued` 记录已提交数据库，尚未表示设备执行 |
| `Duplicate` | 相同 ID、相同规范化载荷已登记，JSON 返回已有状态 |
| `Result` | 查询或只读核验请求的当前记录，JSON 包含状态、原因和时标 |
| `Rejected` / `Invalid` | 业务拒绝、ID 载荷冲突或格式非法 |
| `RetryLater` | 停机/热加载期间暂停受理，或核验队列暂不可用 |
| `Unknown` | 数据库提交或通信结果不能确认，用原 ID 查询或重试交接 |

查询/核验载荷分别仅为 `{"op":"query"}`、`{"op":"verify"}`。
线协议沿用 `EMS-CMD/1\0command_id\0JSON` 和 `EMS-ACK/1\0状态字节+command_id\0正文`；
查询回复增加状态字节 `S`。查询回复本身不等于设备成功。

## 调度和执行状态

每条独立连接有最多 128 个内存任务，每个调度点最多执行一个控制任务。
空闲等待可以被命令唤醒；Modbus 连续地址仍合并为一次有界读事务，在事务之间插队；
IEC104 按最多 64 个缓存点返回调度点。不会打断已经发送的帧，也不会并发操作协议句柄。
数据库待发记录按 ID 分页扫描，满队列留在持久 inbox，超过期限后变为 `Expired`。

| 执行状态 | 含义与后续动作 |
| --- | --- |
| `Queued` | 等待路由及工作线程；物理发送前复核期限、修订号并提交条件执行登记 |
| `Executing` | 发送意图已持久提交；可能正在设备 I/O 中 |
| `Acked` | 插件返回协议确认；未配置反馈时停留于此，不宣称设备状态已核验 |
| `Succeeded` | 配置的全部必需反馈判据由发送后的新样本满足 |
| `Rejected` / `Expired` | 可以证明本次未发送：配置变化、停止、量程非法或到期等 |
| `Unknown` | 设备动作可能已发生；只安排读反馈，绝不自动重写 |

持久提交失败或提交回复丢失时不开始新动作。当前插件写接口只有 bool，写失败无法证明
未发送，保守记录 `Unknown`。重启将旧 `Executing` 转成 `Unknown`；新鲜度边界重新设为
恢复时刻，避免旧反馈误判。传输重复、显式 `verify` 及自动核验均不重发原动作。
停止及热加载收尾队列内未发任务；已进入设备 I/O 的任务等待有界协议调用返回再收敛。

遥调使用 `(工程值-offset)/scale`，先验证工程量程和原码类型范围，整数不静默截断。
遥控支持固定值或直接值。最小间隔在当前连接内按点/写组限制；同连接写入天然互斥。
脉冲复位策略尚未实现，`pulse_width_ms != 0` 明确拒绝，防止只发置位而不复位。

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
