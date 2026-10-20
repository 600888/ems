# 文档维护

- [文档组织与 Rspress 接入](documentation.md)：本地启动、构建、主题、导航维护与旧路径对照。

站点与 dlt698 一样独立放在 `website/`，文档内容位于 `website/docs/`。使用 Node.js 22.12+，
先进入 `website/`，再执行 `npm ci` 和 `npm run dev` 启动本地站点。生产构建使用
`npm run build`，输出到 `website/doc_build/`；完整操作说明见上文链接。
