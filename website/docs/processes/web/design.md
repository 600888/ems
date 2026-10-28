# Web 进程设计

> 日期：2026-10-24｜状态：首期 Web 与设备模块已实现；[构建运行](running.md)。[设备设计](device/index.md) · [API 契约](device/api.md)。

登录及用户管理的目标设计见[用户名、密码与加密登录](user/login.md)和[用户管理与角色权限](user/index.md)。当前仍为内存会话骨架；下文身份目标约定不表示持久账号已经实现。

## 1. 进程与业务模块

```mermaid
flowchart TB
    UI[Vue 前端] --> HTTP[Drogon HTTP 入口]
    HTTP --> Identity[identity 身份与权限]
    HTTP --> Device[device 设备配置]
    HTTP --> Health[health 存活与就绪]
    HTTP -.后续.-> Monitor[monitoring 实时监视]
    HTTP -.后续.-> History[history 历史查询]
    HTTP -.后续.-> Commands[commands 控制意图]
    Device --> Repo[公共 Repository]
    Repo --> DB[(MySQL)]
    Monitor --> RT[公共实时客户端]
    History --> Storage[存储进程 RPC]
    Commands --> Intent[持久意图与 outbox]
    Intent --> Strategy[策略仲裁与采集执行]
```

| 模块 | 所有权 | 依赖边界 |
| --- | --- | --- |
| identity | 登录、会话、主体与资源权限 | 对外提供 Principal / Authorization |
| device | 树、档案、绑定、四遥、模板、导入 | 复用公共模型和数据库层 |
| health | 进程存活、依赖就绪、指标 | 不探测现场设备 |
| channels（后续） | 通道连接与通信管理 | 首期由 device 承接，移交时复用规则与事务 |
| monitoring / history / commands（后续） | 实时、历史、人工意图 | 不通过配置接口执行设备命令 |

配置保存即生效，以站根 `config_revision` 触发热加载；不建立配置草稿、发布、历史或回滚。

## 2. 目录与依赖

按业务建目录，模块内再分层。以下为目标布局，仅创建本期实际使用的文件。

```text
backend/src/web/
  main.cpp
  CMakeLists.txt
  app/
    web_application.h/.cpp        # 启停与依赖装配
    web_options.h/.cpp            # 配置与预算
    module_registry.h/.cpp        # 显式注册模块
  transport/
    request_context.h/.cpp        # 主体、trace、期限、回复所有权
    json_reader.h/.cpp            # JSON 读取与字段白名单
    response_writer.h/.cpp        # 统一响应与错误
    filters/                      # 身份、CSRF、准入
  platform/
    executor/bounded_executor.h/.cpp
    database/database_factory.h/.cpp
    observability/                # 日志与指标
  modules/
    identity/
      module.h/.cpp
      api/                        # 登录与会话入口
      application/                # 会话服务
      public/authorization.h      # 跨模块契约
      infrastructure/             # 账号与会话实现
    device/
      module.h/.cpp
      api/                        # device/binding/point/import/catalog_controller
      application/                # 对应服务与 configuration_writer
      validation/                 # 请求与导入选项检查
      infrastructure/             # 文件工作区、纯解析器适配
      public/device_query.h       # 只读设备定位
    health/
      module.h/.cpp
      api/health_controller.h/.cpp
backend/conf/web.json
backend/tests/web/                # application / transport / integration
```

```mermaid
flowchart LR
    App[app 显式装配] --> API[模块 api]
    App --> Adapter[模块 infrastructure]
    API --> Transport[transport 公共入口能力]
    API --> Service[模块 application]
    Service --> Public[其他模块 public 契约]
    Service --> Common[common 模型与 Repository 接口]
    Adapter --> Parser[公共纯解析器]
    DB[common 数据库实现] --> Common
    App --> DB
```

| 规则 | 落点 |
| --- | --- |
| Controller 只解析、调用、编码 | 不包含 ORM、SQL、协议插件 |
| Service 管用例、授权、事务 | 不依赖 Drogon 类型或全局容器 |
| 模块之间只调 public | 不包含其他模块私有头文件，不形成循环 |
| SQL、行结构、ORM 只有一份 | `include/common/database` + `src/common/db` |
| Web 内部头文件与实现相邻 | 不复制到 include/web；接口写 Doxygen，遵守 `.clang-format` |

构建：Drogon + C++17，目标显式指定标准；新增 `EMS_BUILD_WEB`（初始 OFF），开启缺依赖则失败。纯应用测试不链接 Drogon；设备配置不链接采集引擎。Windows/MSVC 与 Linux/ARM 分别验收。

## 3. HTTP 统一入口

```mermaid
flowchart TD
    Request[HTTP 请求] --> Method{方法}
    Method -->|GET| Empty{无业务参数且无请求体}
    Empty -->|是| Read[无参数只读接口]
    Empty -->|否| Bad[400 参数错误]
    Method -->|POST| JSON[application/json 请求体]
    JSON --> Route[固定路径分派用例]
    Route --> Query[查询：list / detail / tree]
    Route --> Write[操作：create / update / delete / apply]
    Method -->|其他方法| Reject[405]
```

| 约定 | 内容 |
| --- | --- |
| 路径 | 固定 `/api/模块/动作`，不嵌入 ID，不使用 URL 查询串 |
| 参数 | 有参数一律 POST + JSON；GET 只用于无参数读取 |
| 并发参数 | expected_config_revision、expected_preview_hash 均放 JSON |
| 上传例外 | multipart：file 为二进制文件，metadata 为 JSON 业务参数 |
| 身份 | cookie、认证/CSRF 等安全头保留，不承载业务筛选与 ID |
| 编码 | UTF-8、snake_case；对象/数组直接传 JSON，不传转义 JSON 字符串 |
| JSON 库 | 业务、配置、DTO 与序列化统一 nlohmann::json；JsonCpp 仅为 Drogon 内部依赖，不进入业务接口 |
| 类型 | 稳定 ID、revision、int64/uint64 为字符串；布尔为 boolean；缺失为 null |
| 响应 | 成功 `{data, meta}`；失败 `{error, meta}`；时间为 UTC RFC 3339 |

浏览器 Fetch 不允许 GET 请求体，因此需要参数的读取也使用 POST，见 [Fetch 规范](https://fetch.spec.whatwg.org/#dom-request)。POST 查询仍按只读权限授权，不增加 revision；响应使用 `Cache-Control: no-store`。API 未匹配返回 JSON 404，不能回退 SPA。

## 4. 请求与工作线程

```mermaid
sequenceDiagram
    participant B as 浏览器
    participant I as Drogon I/O
    participant Q as 有界工作池
    participant S as 应用服务
    participant D as 公共数据库
    B->>I: 固定 URL + JSON
    I->>I: 大小、方法、结构、会话、准入
    I->>Q: 非阻塞入队
    alt 队列已满
        I-->>B: 503 + Retry-After
    else 接收任务
        Q->>S: DTO + Principal + CallContext
        S->>D: 授权后查询或事务写入
        D-->>S: Result + effect
        S-->>I: 结果
        I-->>B: 一次性回复 JSON
    end
```

| 资源 | 默认预算 | 触限 |
| --- | --- | --- |
| I/O 线程 / 普通线程 / 队列 | 2 / 4 / 128 | 满队列 503 |
| 导入线程 / 队列 / MySQL 连接 | 1 / 8 / 4 | 有界等待；解析不占连接 |
| 普通请求 / 导入 / 退出宽限期 | 5 s / 30 s / 15 s | 超时保留写入确定性 |
| 普通 JSON / 导入 metadata | 1 MiB / 64 KiB | 413，读取前限制 |
| 导入文件 / multipart 整体 / XLSX 解压累计 | 20 MiB / 21 MiB / 100 MiB | 超限拒绝，不截断 |
| 导入行 / 批量改点 | 50,000 / 200 | 超限 422 |
| 列表 / 树分页 | 默认 20/100，最大 200/500 | 非法值 400 |

上传分别限制 multipart 总体、metadata、文件与解压量；有效上限取解析器和存储能力的交集。文件名不作磁盘路径，服务端随机工作区不对外静态开放。

- 数据库、密码哈希、文件/解析、实时锁等待和长 RPC 在工作池执行；I/O 线程不做同步业务。
- 排队计入 deadline；事务由同一线程创建和结束；超时与完成路径只能回复一次。
- CallContext 现有字段为 deadline/trace_id，取消状态属于 Web 上下文；不强行销毁在途事务。

## 5. 启停、身份与异常

```mermaid
flowchart LR
    Start[读取配置与预算] --> Check[公共工厂连接数据库并查 14 表]
    Check --> Compose[装配身份与模块]
    Compose --> Listen[监听并置 ready]
    Listen --> Stop[退出：取消 ready 与新准入]
    Stop --> Drain[排空或取消排队任务]
    Drain --> Close[所属线程结束事务后关闭依赖]
    Check -->|失败| Exit[非零退出；不自动建表]
```

| 项目 | 约定 |
| --- | --- |
| 连接 | 沿用 EMS_DATABASE_CONFIG / EMS_MYSQL_*；Web 不复制数据库配置结构 |
| 身份 | 目标为本地用户名/密码、HTTPS、密码哈希、公共账号及三类角色和资源范围，见[登录设计](user/login.md)；运行配置不含账号清单 |
| 会话 | 当前内存会话重启失效；目标持久会话，浏览器持 HttpOnly/Secure/SameSite=Strict 的不透明 Cookie，后端读取当前权限 |
| 授权 | 按数据库实际关系校验站、设备子树、绑定和任务，过滤列表与 total |
| CSRF | POST 查询与写入统一检查；登录检查 Origin；后续 WebSocket 也检查来源 |
| 审计 | 同公共事务对接统一审计；不在 Web 建私有审计表 |
| 健康 | GET `/health/live`、`/health/ready`；依赖缺失返回 503，采集离线不阻断配置 |
| 身份就绪 | 当前未装配账号服务可启动但拒绝受保护请求；目标生产部署必须启用身份模块，身份表/密钥等依赖缺失时 ready 返回 503 |
| 部署 | 同源；Drogon 静态服务或 Nginx 唯一入口；只信任配置中的代理 |

```mermaid
flowchart TD
    Result[调用结果] --> OK{成功}
    OK -->|是| Success[data + meta]
    OK -->|否| Error[error + meta]
    Error --> Input[400 格式 / 422 业务规则]
    Error --> Auth[401 身份 / 403 权限 / 404 不可见资源]
    Error --> Conflict[409 并发或引用冲突]
    Error --> Limit[413 大小 / 429 配额 / 503 依赖或容量]
    Error --> Failure[504 期限 / 500 内部或数据异常]
    Error --> Effect[effect：NotApplicable / NotApplied / Unknown]
```

`Unknown` 表示可能已提交，按原稳定 ID/任务查询；不自动更换 ID 重试。日志不记录凭据、cookie、完整文件或 DSN。

## 6. 实施顺序

```mermaid
flowchart LR
    A[进程基础与身份] --> B[树与档案]
    B --> C[通道绑定]
    C --> D[四遥与保存事务]
    D --> E[导入预览与应用]
    E --> F[前端关闭 mock 联调]
```

验收重点：真实分页、资源越权、站根锁并发、整批回滚、重复导入、过载与退出。MySQL 锁/约束单独验证；后续实时/历史依赖不作为首期配置就绪条件。
