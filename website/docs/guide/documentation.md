# 文档组织与 Rspress 接入

> 更新日期：2026-10-20｜范围：站点运行、主题、导航元数据、相对链接和设计一致性。

## 1. 归档规则

| 目录 | 放什么 | 不放什么 |
| --- | --- | --- |
| architecture | 系统边界、设备等跨进程业务模型 | 第三方库函数手册 |
| processes/collector | 采集与上送进程，通道调度、协议实例/会话、分发与执行 | 另一份公共设备或 Redis 键规范 |
| processes/data | 常驻实时后端管理、共享段生命周期、接入、健康与受控维护 | 每次数据读写代理、聚合算法、历史与控制仲裁 |
| processes/storage | 归档、配置/outbox 发布、查询与恢复 | 现场协议、策略算法 |
| processes/strategy | 状态聚合、告警、优化调度、仲裁 | 厂家原始点地址 |
| processes/web | Drogon API、权限、配置页面和前端协作 | 直接设备控制和数据库内部连接对象 |
| common | 共享模型的代码组织、跨进程复用技术接口、数据契约与实施规范 | 设备模型专题的重复副本、归属不明的业务逻辑 |
| decisions | 技术选型理由、前提与验证条件 | 与正式接口相冲突的隐含新约定 |
| reference | 第三方 SDK、协议库使用参考 | 声称示例已经过现场验收 |

跨进程主题保留一个权威专题，各进程页引用并说明其自身责任。总架构说明决策和边界，详细接口由对应专题定义。出现冲突先同步修改，不依赖读者自行猜测哪个文件更新。

## 2. 页面、命名与链接

- 目录和文件名使用小写英文及连字符，如 `channel-protocol.md`；标题与正文使用中文，产品术语保持一致。
- 首页使用 `index.mdx` 展示模块卡片；其余目录维护 `index.md` 和 `_meta.json`，不同时使用 README.md 竞争同一目录入口。
- 相对 Markdown 链接显式带 `.md`，例如 `../../architecture/channel-protocol.md`；不要写某台开发机的绝对磁盘路径。
- `docs/` 之外的仓库文件使用 GitHub 文件链接，避免部署后指向不存在的站内路由。
- 一个页面只有一个一级标题；示例、ASCII 图、JSON 和 C++ 使用代码围栏。代码中的尖括号、花括号不裸露在 MDX 正文中。
- 重要专题记录版本/日期/状态；规范、建议、未实现能力、第三方示例分别注明。
- 图片如需添加，放明确的资源目录并使用相对链接；不得引用开发机临时目录。
- 移动文件同时更新入链、出链和导航；不复制同一篇文档分别维护。旧公共 URL 是否重定向由正式站点路由策略决定。

## 3. 启动、构建与主题

站点工程与 dlt698 一样独立放在 `website/`，使用相同的 **Rspress 2.0.21**。准备
Node.js 22.12+，在仓库根目录执行：

```bash
cd website
npm ci
npm run dev
```

打开终端显示的本地地址。修改 `website/docs/` 内容会自动刷新；修改配置或导航元数据后，如未刷新，重启开发服务。以下命令均在 `website/` 内执行。

```bash
npm run typecheck
npm run build
npm run preview
```

`build` 输出 `website/doc_build/`，可由静态服务器托管。`preview` 用于本地检查构建产物。
依赖锁文件为 `website/package-lock.json`，更换环境优先使用 `npm ci`。

站点源码、配置、依赖及构建产物均收在 `website/`，结构与 dlt698 保持一致：

```text
website/
  docs/                 Markdown / MDX 内容、导航与 public 资源
  theme/                自定义主题与首页卡片
  package.json
  package-lock.json
  rspress.config.ts
  tsconfig.json
  node_modules/         本地依赖，Git 忽略
  doc_build/            静态构建产物，Git 忽略
```

主题沿用 dlt698 的蓝色配色、字体、宽屏三栏布局、首页模块卡片及移动端断点。下表路径均相对于 `website/`：

| 文件 | 用途 |
| --- | --- |
| `rspress.config.ts` | 标题、Logo、导航适配、搜索、编辑链接与 Mermaid 插件 |
| `theme/index.css` | 从 dlt698 主题迁入的配色、排版与响应式布局 |
| `theme/index.tsx` | 加载样式并导出 Rspress 原主题 |
| `theme/ModuleCards.tsx` | EMS 首页的六个模块卡片 |
| `docs/public/logo.svg` | EMS 站点标识 |

导航仍以 `docs/_nav.json`、根及子目录 `_meta.json` 为内容来源。配置读取这些文件生成
`themeConfig.nav/sidebar`，实现与 dlt698 一致的分模块侧边栏：左侧提供返回首页、模块标题和当前模块目录。
新增页面时更新所在目录的 `_meta.json`；新增顶层模块还要更新根 `_meta.json`、`_nav.json` 和首页卡片。
无需在配置中再维护一份文章清单。Rspress 原生自动导航规则见
[自动导航](https://www.rspress.dev/guide/basic/auto-nav-sidebar)。

默认部署根路径为 `/`。托管到 `/ems/` 等子目录时，在构建和预览命令中使用相同的 `DOCS_BASE`。
PowerShell 示例：

```powershell
$env:DOCS_BASE = '/ems/'
npm run build
npm run preview
```

Linux/macOS 示例：`DOCS_BASE=/ems/ npm run build`。构建产物需要在同名子路径下提供服务。

EMS 的 GitHub Pages 站点地址为 <https://600888.github.io/ems/>，工作流以 `/ems/` 为构建基路径，
并将 `website/doc_build/` 发布到 `gh-pages` 分支。合并到 `main` 的 `website/` 变更会自动触发发布；
Pull Request 会执行构建检查但不会发布。首次启用仓库 Pages 时，在 GitHub 仓库的 **Settings → Pages** 中
将构建来源设为 `Deploy from a branch`，分支选择 `gh-pages`、目录选择 `/(root)`。

文件到路由的规则和相对文件链接参考 [Rspress 路由](https://www.rspress.dev/guide/basic/conventional-route)、[Rspress 链接](https://www.rspress.dev/guide/use-mdx/link)。本文的 Markdown 链接同时便于仓库内阅读。

## 4. Mermaid 与实际渲染

架构图、关系图和时序图保留为 `mermaid` 围栏，由已配置的 `rspress-plugin-mermaid` 1.0.3 在浏览器中渲染。
无需手工将图转成图片；图表语法错误需要在页面中检查。插件来源见
[Rspress 社区插件](https://www.rspress.dev/plugin/community-plugins/overview)。

构建已开启内部链接及锚点检查，引用不存在的页面或标题会导致构建失败。
验收还应包含：中文标题、目录入口、分模块侧边栏顺序、跨目录链接、Mermaid 三种图、代码高亮、
宽表格/ASCII 图横向滚动和窄屏布局。

## 5. 本次路径调整

以下旧路径仅作迁移说明，均相对于 docs；新路径为唯一维护位置。

| 旧路径 | 新路径/处理 |
| --- | --- |
| `README.md`、根 `index.md` | `index.mdx`，文档首页及模块卡片 |
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
