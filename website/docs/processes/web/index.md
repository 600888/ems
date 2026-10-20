# Web 后端与前端协作

> 后端：`ems-web` / C++17 + Drogon｜前端：Vue 3 + TypeScript + Tailwind CSS｜状态：目标设计。

`ems-web` 使用 [Drogon](https://github.com/drogonframework/drogon) 提供 REST API、WebSocket、路由和请求过滤器，负责身份认证、站点/资产/操作授权、配置编辑与发布入口、业务查询及展示编排。MySQL 业务访问继续复用公共 ormpp Repository，避免为 Web 维护另一套表映射。前端只访问 `ems-web`，不直连现场、Redis、共享内存、MySQL 或内部 gRPC。

## 进程内分层与资源边界

```text
HTTP/WebSocket → Drogon 路由与鉴权过滤器 → Web Service
                                             ├→ 公共 Repository / ormpp → MySQL
                                             ├→ 实时查询契约 → Redis 或存储 gRPC
                                             └→ HistoryService gRPC → 存储进程 → TAOS
```

Controller 只做输入解析、身份/权限上下文传递、错误映射和响应；版本冲突、点位能力、控制受理等业务规则在 Service 与公共模型中校验。每个进程独立创建数据库连接池与 gRPC 客户端；一次业务写入及其 outbox 在同一 MySQL 连接和事务提交。Drogon 的网络 I/O 线程不执行同步 ormpp 查询、文件导入或长历史请求：这些任务进入有界业务工作池，设排队上限、截止时间和取消处理。请求超时不等于事务未提交，客户端按原幂等键查询结果。

登录会话或令牌、角色及站点/资产授权由 Web 进程核验；命令控制权、设备所有权和执行互锁仍由策略与采集按权威状态检查。WebSocket 只推送变化提示和展示数据，断线重连后重新读取权威快照；不能以推送成功替代审计持久化。

## 页面与关系

| 页面 | 配置内容 | 应展示的关联 |
| --- | --- | --- |
| 设备 | 类型/型号、固有点、厂家表、禁用与反馈 | 资产、采集/上送绑定、当前能力 |
| 通道 | 用途、EMS 角色、协议 profile、资源 | 设备、点映射、peer、运行会话 |
| 协议 | 版本/服务、参数 schema、模型文件、限额 | 支持角色/传输、引用此版本的通道 |
| 上送点表 | 标准点、对外地址、编码/质量/控制权限 | 数据来源、目标通道、可见 peer |
| 发布/运行 | 差异、校验、发布与回滚 | 已保存/已发布/各实例已应用、错误与会话状态 |

设备页和通道页编辑同一套绑定，不独立保存两份列表。设备可以同时关联采集和上送；不提供“设备只能是客户端或服务端”的互斥选项。协议能力由后端目录提供，UI 不能自行开放未实现选项。

## API 资源划分

以下为设计示例；更新携带期望版本，列表分页并按 station/资产鉴权。64 位 ID、时间和计数器在 JSON 中使用无损十进制文本，前端不能经 JavaScript Number 再参与版本比较。

| API | 职责 |
| --- | --- |
| `/api/v1/devices` 及标准点/导入子资源 | 设备与厂家点表、标准绑定 |
| `/api/v1/protocols` | 只读能力目录：角色、传输、服务与 schema |
| `/api/v1/protocol-profiles` | 不可变协议参数版本 |
| `/api/v1/channels` | 通道；子资源 bindings、forwarding-maps、peers |
| `/api/v1/channels/{channel_id}/sessions` | 只读运行会话诊断；session 不是设备 |
| `/api/v1/config-drafts/{draft_id}/validate`、`/publish` | 完整依赖校验与发布 |
| `/api/v1/control-intents` | 人工意图，不接受任意寄存器/函数写目标 |
| `/api/v1/points/latest`、`/history` | 标准实时/历史数据，保留质量、时间和版本 |

规则以 [设备模型](../../architecture/device-model.md) 和 [通道协议](../../architecture/channel-protocol.md) 为准。公共 Repository 同事务保存业务/配置和 outbox，存储异步发布；Web 不伪造其他实例的 applied 状态。

## 查询、推送与验收

MySQL 经本进程公共 ormpp Repository；历史经存储 gRPC。Redis 部署可按契约读实时值，共享内存部署经存储实时查询 gRPC，页面字段不变。推送断开后重新读快照，失效值不填零。内部 RPC 与业务工作池分别限制并发、行数、时长和排队长度，大报表不能占用控制受理预算。静态前端可由 Drogon 或可选 Nginx 提供，部署时只选择一个对外入口。

控制区分受理、仲裁、发送、协议确认、反馈成功、失败、过期与未知；北向控制也进入同一审计视图。验收覆盖双向编辑一致、跨站越权、角色非法、地址冲突、依赖删除、版本冲突、重复提交/超时结果查询、部分发布、WebSocket 重连和后端切换。目标 Drogon 版本需在 Linux/ARM 与项目 C++17 工具链上验证 HTTP、WebSocket、TLS 和客户端依赖；本文不表示 `ems-web` 已实现。
