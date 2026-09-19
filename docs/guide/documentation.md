# 文档组织与 Rspress 接入

> 整理日期：2026-09-19｜范围：内容目录、导航元数据、相对链接和设计一致性。

## 1. 归档规则

| 目录 | 放什么 | 不放什么 |
| --- | --- | --- |
| architecture | 系统边界、设备等跨进程业务模型 | 第三方库函数手册 |
| processes/collector | 采集与上送进程，通道调度、协议实例/会话、分发与执行 | 另一份公共设备或 Redis 键规范 |
| processes/storage | 归档、配置/outbox 发布、查询与恢复 | 现场协议、策略算法 |
| processes/strategy | 状态聚合、告警、优化调度、仲裁 | 厂家原始点地址 |
| processes/web | Go API、权限、配置页面和前端协作 | 直接设备控制和数据库内部连接对象 |
| common | 共享模型的代码组织、跨进程复用技术接口、数据契约与实施规范 | 设备模型专题的重复副本、归属不明的业务逻辑 |
| decisions | 技术选型理由、前提与验证条件 | 与正式接口相冲突的隐含新约定 |
| reference | 第三方 SDK、协议库使用参考 | 声称示例已经过现场验收 |

跨进程主题保留一个权威专题，各进程页引用并说明其自身责任。总架构说明决策和边界，详细接口由对应专题定义。出现冲突先同步修改，不依赖读者自行猜测哪个文件更新。

## 2. 页面、命名与链接

- 目录和文件名使用小写英文及连字符，如 `channel-protocol.md`；标题与正文使用中文，产品术语保持一致。
- 每个目录维护 `index.md` 和 `_meta.json`，不同时使用 README.md 竞争同一目录入口。
- 相对 Markdown 链接显式带 `.md`，例如 `../../architecture/channel-protocol.md`；不要写某台开发机的绝对磁盘路径。
- 一个页面只有一个一级标题；示例、ASCII 图、JSON 和 C++ 使用代码围栏。代码中的尖括号、花括号不裸露在 MDX 正文中。
- 重要专题记录版本/日期/状态；规范、建议、未实现能力、第三方示例分别注明。
- 图片如需添加，放明确的资源目录并使用相对链接；不得引用开发机临时目录。
- 移动文件同时更新入链、出链和导航；不复制同一篇文档分别维护。旧公共 URL 是否重定向由正式站点路由策略决定。

## 3. Rspress 导航准备

本次已提供根 `_nav.json`、根及子目录 `_meta.json`。根导航面向架构、进程、公共模块、技术决策、开发参考与文档维护；侧边栏按实际文件顺序显式列出页面和目录。

当前 Rspress 文档说明 `_nav.json` 和 `_meta.json` 可自动生成导航/侧边栏；启用这种方式时，不应同时在配置中手工声明覆盖它们的 nav/sidebar。根 `_meta.json` 用于全局侧边栏，详见 [Rspress 自动导航](https://www.rspress.dev/guide/basic/auto-nav-sidebar)。

后续站点工程最小配置可以采用：

```ts
import { defineConfig } from '@rspress/core';

export default defineConfig({
  root: 'docs',
  title: 'EMS 设计文档',
  description: 'EMS 系统、进程与公共接口设计',
  lang: 'zh-CN',
});
```

这是未来工程的配置示例，本次未添加 npm 工程、安装 Rspress 依赖或执行站点构建。应在工程中锁定经验证的 Rspress 及插件版本，按实际部署设置站点 base 和域名；不要把“元数据可解析”当成“站点已构建成功”。

文件到路由的规则和相对文件链接参考 [Rspress 路由](https://www.rspress.dev/guide/basic/conventional-route)、[Rspress 链接](https://www.rspress.dev/guide/use-mdx/link)。本文的 Markdown 链接同时便于仓库内阅读。

## 4. Mermaid 与实际渲染

架构图、关系图和时序图保留为 `mermaid` 围栏。接入时需要为选定 Rspress 版本配置兼容的 Mermaid 渲染方式；官方社区插件目录列有相关插件，见 [Rspress 社区插件](https://www.rspress.dev/plugin/community-plugins/overview)。不假设纯 Markdown 内容复制过去就一定能显示图形。

站点接入验收至少包含：中文标题/锚点、目录入口、全局侧边栏顺序、跨目录链接、Mermaid 三种图、代码高亮、宽表格/ASCII 图横向滚动和窄屏布局。启用构建期内部链接/锚点检查的配置以使用版本为准。

## 5. 本次路径调整

以下旧路径仅作迁移说明，均相对于 docs；新路径为唯一维护位置。

| 旧路径 | 新路径/处理 |
| --- | --- |
| `README.md` | `index.md`，扩充为文档首页 |
| `EMS架构设计.md` | `architecture/system-design.md` |
| `EMS设备模型与点表绑定设计.md` | `architecture/device-model.md` |
| `collect/data_collect_architecture.md` | `processes/collector/design.md` |
| `rtdb/index.md` | `common/realtime/index.md` |
| `rtdb/公共实时数据访问接口设计.md` | `common/realtime/access-design.md` |
| `rtdb/Redis键设计.md` | `common/realtime/redis-key-design.md` |
| `ZeroMQ引入评估.md` | `decisions/zeromq-assessment.md` |
| `protocol/libmodbuspp_guide.md` | `reference/protocol/libmodbuspp-guide.md` |
| `collect/libmodbuspp_guide.md` | 与上一项逐字节一致，合并为一份 |
| `data_acquisition/data_acquisition.md` | 原为空文件，移除占位，由通信进程入口代替 |

本次未恢复此前已删除的旧版架构文档。后续需要保留历史发布版时，应按站点版本机制或 Git 历史管理，不在当前导航中混放多份互相冲突的规范。

## 6. 持续维护检查

每次改动检查：相对文件链接存在、锚点与标题一致、JSON 导航可解析、导航无缺页/重页、目录无未说明空文件、代码围栏配对。涉及 API 草案时独立验证声明的语言标准；涉及 Mermaid 时在正式站点中渲染验证。

协议角色或数据模型调整时，至少同步系统架构、设备模型、通道协议专题、受影响进程页及数据契约；第三方库能力变更只更新参考，不自动扩大可发布的业务能力。
