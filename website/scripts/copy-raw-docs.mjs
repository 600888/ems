/**
 * 把 docs 下所有 Markdown 源文件同步到 docs/public/raw/，
 * 供前端"导出 Markdown"按钮直接下载（保持目录结构）。
 * 由 package.json 的 predev / prebuild 钩子自动执行。
 *
 * 采用差量同步：只覆盖变化文件、只删除源里已不存在的文件，
 * 避免每次运行整目录删除（沙箱/安全策略对批量删除有阈值限制）。
 */
import { cpSync, mkdirSync, readdirSync, rmdirSync, statSync, unlinkSync } from 'node:fs'
import path from 'node:path'

const root = path.resolve(import.meta.dirname, '..')
const docsDir = path.join(root, 'docs')
const outDir = path.join(docsDir, 'public', 'raw')

/** 递归收集 {相对路径: 绝对路径} 映射 */
function collect(dir, rel = '', acc = new Map()) {
  for (const name of readdirSync(dir)) {
    const abs = path.join(dir, name)
    const st = statSync(abs)
    if (st.isDirectory()) {
      collect(abs, path.join(rel, name), acc)
    } else {
      acc.set(path.join(rel, name), abs)
    }
  }
  return acc
}

function collectDocs() {
  const acc = new Map()
  for (const name of readdirSync(docsDir)) {
    const abs = path.join(docsDir, name)
    const st = statSync(abs)
    if (st.isDirectory()) {
      if (name === 'public') continue
      collect(abs, path.join(name, ''), acc)
    } else if (/\.(md|mdx)$/.test(name)) {
      acc.set(name, abs)
    }
  }
  return acc
}

const sources = collectDocs()

// 目标目录当前内容（仅收集 md/mdx，其余不动）
const targets = new Map()
if (statSyncSafe(outDir)) {
  for (const [rel, abs] of collect(outDir)) {
    if (/\.(md|mdx)$/.test(rel)) targets.set(rel, abs)
  }
}

let copied = 0
let removed = 0

for (const [rel, src] of sources) {
  const dest = path.join(outDir, rel)
  mkdirSync(path.dirname(dest), { recursive: true })
  cpSync(src, dest)
  copied++
}

// 删除源里已消失的文件（差量，通常为 0 个）
for (const rel of targets.keys()) {
  if (!sources.has(rel)) {
    unlinkSync(path.join(outDir, rel))
    removed++
  }
}

// 自底向上清理空目录
pruneEmptyDirs(outDir)

console.log(`[export-raw-docs] synced ${copied} markdown files to docs/public/raw (${removed} removed)`)

function statSyncSafe(p) {
  try {
    return statSync(p).isDirectory()
  } catch {
    return false
  }
}

function pruneEmptyDirs(dir) {
  let empty = true
  for (const name of readdirSync(dir)) {
    const abs = path.join(dir, name)
    if (statSync(abs).isDirectory()) {
      if (pruneEmptyDirs(abs)) rmdirSync(abs)
      else empty = false
    } else {
      empty = false
    }
  }
  return empty
}
