# Web 后端与前端协作

> 后端：`ems-web` / Go｜前端：Vue 3 + TypeScript + Tailwind CSS｜状态：目标设计。

Go 负责身份、站点/资产/操作权限、配置编辑、API 与展示编排。前端只访问 Go 的 REST/WebSocket，不直连现场、Redis、共享内存或内部 gRPC。

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

以下为设计示例；更新携带期望版本，列表分页并按 station/资产鉴权。

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

规则以 [设备模型](../../architecture/device-model.md) 和 [通道协议](../../architecture/channel-protocol.md) 为准。GORM 同事务保存业务/配置和 outbox，存储异步发布；Go 不伪造其他实例的 applied 状态。

## 查询、推送与验收

MySQL 经本进程 GORM；历史经存储 gRPC。Redis 部署可按契约读实时值，共享内存部署改经存储实时查询 gRPC，页面字段不变。推送断开后重新读快照，失效值不填零。

控制区分受理、仲裁、发送、协议确认、反馈成功、失败、过期与未知；北向控制也进入同一审计视图。验收覆盖双向编辑一致、跨站越权、角色非法、地址冲突、依赖删除、版本冲突、部分发布和后端切换。
