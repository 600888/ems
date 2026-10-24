import { readFileSync } from 'node:fs'
import path from 'node:path'
import { defineConfig } from '@rspress/core'
import mermaid from 'rspress-plugin-mermaid'
import nav from './docs/_nav.json'
import modules from './docs/_meta.json'

interface MetaItem {
  type: 'file' | 'dir'
  name: string
  label: string
  collapsible?: boolean
  collapsed?: boolean
}

interface SidebarItem {
  text: string
  link?: string
  collapsible?: boolean
  collapsed?: boolean
  items?: SidebarItem[]
}

// Keep page names and ordering in _meta.json while using dlt698's per-module sidebars.
function readSidebar(directory: string): SidebarItem[] {
  const meta: MetaItem[] = JSON.parse(
    readFileSync(path.join(import.meta.dirname, 'docs', directory, '_meta.json'), 'utf8'),
  )
  return meta.map((item) => {
    const route = `/${directory}/${item.name}`
    if (item.type === 'dir') {
      return {
        text: item.label,
        collapsible: item.collapsible,
        collapsed: item.collapsed,
        items: readSidebar(`${directory}/${item.name}`),
      }
    }
    return { text: item.label, link: item.name === 'index' ? `/${directory}/` : route }
  })
}

const sections = modules.filter((item) => item.type === 'dir')

export default defineConfig({
  root: 'docs',
  base: process.env.DOCS_BASE || '/',
  title: 'EMS 设计文档',
  description: '储能 EMS 的系统架构、进程设计、公共接口与协议接入文档。',
  lang: 'zh',
  icon: '/logo.svg',
  logo: '/logo.svg',
  logoText: 'EMS',
  route: { cleanUrls: true },
  markdown: { link: { checkDeadLinks: true, checkAnchors: true } },
  plugins: [mermaid()],
  themeConfig: {
    darkMode: 'light',
    nav: nav.map((item) => ({ ...item, position: 'left' as const })),
    sidebar: {
      '/': [
        { sectionHeaderText: '开始阅读' },
        { text: '文档首页', link: '/' },
        { text: '文档组织与 Rspress', link: '/guide/documentation' },
        { dividerType: 'solid' },
        { sectionHeaderText: '文档模块' },
        ...sections.map((item) => ({ text: item.label, link: `/${item.name}/` })),
      ],
      ...Object.fromEntries(
        sections.map((item) => [
          `/${item.name}/`,
          [
            { text: '返回文档首页', link: '/' },
            { dividerType: 'solid' },
            { sectionHeaderText: item.label },
            ...readSidebar(item.name),
          ],
        ]),
      ),
    },
    search: true,
    lastUpdated: true,
    editLink: { docRepoBaseUrl: 'https://github.com/600888/ems/tree/main/website/docs' },
    socialLinks: [{ icon: 'github', mode: 'link', content: 'https://github.com/600888/ems' }],
    enableScrollToTop: true,
    llmsUI: false,
  },
})
