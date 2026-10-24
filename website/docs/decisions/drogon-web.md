# Web 后端采用 Drogon

> 决策日期：2026-09-28｜边界更新：2026-10-23｜状态：目标架构决策，尚未完成实现与目标平台验收。

## 决策

`ems-web` 使用 C++17 与 Drogon 提供 HTTP API、WebSocket、路由和鉴权过滤器；Vue 3 前端继续通过 Web API 访问系统。业务进程现为采集与上送、实时数据库、存储、策略、Web 五类；实时数据库进程管理实时后端生命周期。先前规划的 Go/GORM Web 后端不再作为实施目标。

MySQL 访问复用[公共数据库访问层](../common/database/)的 ormpp Repository、实体、连接池/事务实现及统一迁移；Web 入口装配独立运行实例。同步查询、导入和长 RPC 进入有界业务工作池，不占 Drogon 网络线程；不启用另一套 Drogon ORM 表模型或维护 Web 私有 SQL。

设备配置按[设备管理数据库设计](../common/database/device-management.md)直接修改工程表，保存即生效，以站根 `config_revision` 触发消费者热加载；不采用配置草稿、发布、快照或回滚。Web 按业务模块分目录，具体布局与生命周期见 [Web 进程设计](../processes/web/design.md)。

业务接口只使用 GET/POST：GET 用于无参数读取，有参数查询和所有操作使用固定路径 POST + JSON，不在 URL 中传业务参数。文件导入保留 multipart，file 为二进制、metadata 为 JSON；详见[设备 API](../processes/web/device/api.md)。

## 依据与边界

[Drogon 官方项目](https://github.com/drogonframework/drogon)提供 HTTP、WebSocket、过滤器及 MySQL 支持，并支持 C++17 和 ARM；这些能力与项目计划的 REST、实时推送和目标工具链相符。统一 C++ 业务模型可减少 Go 与 C++ 两套点位/配置映射，但不能消除进程间版本契约，也不能让 Web 绕过策略仲裁或直接控制设备。

历史查询走存储进程 gRPC；实时查询通过公共门面直接读取同机共享段或权威 Redis，实时数据库进程的管理 RPC 不进入每次读写路径。浏览器只访问 Web，直接映射的信任与平台限制见[实时数据库进程设计](../processes/data/design.md)。控制意图由 Web 使用公共 Repository 与 outbox 在 MySQL 同事务持久保存，最终执行仍由策略仲裁、采集执行并反馈。Drogon 的 HTTP/WebSocket 只负责对外入口，不能充当可靠历史事件总线。

## 实施约束与验收

- 在目标 Linux/ARM 和开发平台锁定并构建 Drogon 版本，验证 HTTP、WebSocket、TLS、MySQL 客户端依赖及 C++17 兼容性。依赖引入与生产配置由实施阶段完成，本文不表示代码已经接入。
- 为 Web Controller、Filter、Service 和公共 Repository 建立单向依赖；请求解析与身份粗查在网络入口，资源授权、工程 revision 并发检查和控制受理在业务层。同步访问必须有有界工作池与过载返回，不能阻塞 Drogon I/O 线程。
- 用共同契约样例验证 C++ 与 TypeScript 的 ID、UTC 微秒、质量、null、版本和错误码映射；64 位整数在浏览器 JSON 中按十进制文本处理。
- 验证用户越权、站根锁与配置并发更新、控制业务记录/outbox 原子性、重复控制意图、gRPC 超时结果查询、WebSocket 断线重连、数据库池耗尽和优雅退出。配置保存本身不依赖 outbox。与其他进程共享表结构时，只使用统一迁移，不由 Web 自动建表。

具体职责和 API 见[Web 后端与前端协作](../processes/web/index.md)，全局进程边界见[系统架构](../architecture/system-design.md)。
