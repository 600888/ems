import { useEffect, useRef, useState } from 'react'

/** 导出菜单：Markdown 源文件下载 / 浏览器打印导出 PDF / DOM 转 Word */

function docTitle(): string {
  const h1 = document.querySelector('.rp-doc h1, .rp-doc-layout__overview h1')
  return h1?.textContent?.trim() || 'document'
}

function safeName(title: string): string {
  const name = title
    .replace(/[\\/:*?"<>|#\s]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 80)
  return name || 'document'
}

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}

/** cleanUrls 路由 -> raw 源文件候选路径（.md / .mdx / 目录 index） */
function rawCandidates(pathname: string): string[] {
  const clean = pathname.replace(/\/+$/, '')
  if (!clean) return ['/raw/index.mdx', '/raw/index.md']
  return [`${clean}.md`, `${clean}.mdx`, `${clean}/index.md`, `${clean}/index.mdx`]
}

async function exportMarkdown() {
  for (const url of rawCandidates(window.location.pathname)) {
    try {
      const res = await fetch(url)
      if (!res.ok) continue
      const text = await res.text()
      if (!text.trim()) continue
      downloadBlob(new Blob([text], { type: 'text/markdown;charset=utf-8' }), `${safeName(docTitle())}.md`)
      return
    } catch {
      // 尝试下一个候选路径
    }
  }
  alert('未找到当前页面的 Markdown 源文件')
}

function exportPdf() {
  window.print()
}

/** 供 Word 导出的 HTML：克隆正文、清理交互元素、SVG 图表替换为占位说明 */
function buildWordHtml(content: Element, title: string): string {
  const clone = content.cloneNode(true) as Element
  clone
    .querySelectorAll('script, style, button, input, nav, [data-export-ignore]')
    .forEach(el => el.remove())
  clone.querySelectorAll('svg').forEach(svg => {
    const p = document.createElement('p')
    p.textContent = '[图表：请在网页版或导出的 PDF 中查看]'
    p.style.color = '#6A737D'
    p.style.fontStyle = 'italic'
    svg.replaceWith(p)
  })
  const body = escapeHtml(title)
  return `<!DOCTYPE html>
<html xmlns:o="urn:schemas-microsoft-com:office:office" xmlns:w="urn:schemas-microsoft-com:office:word">
<head>
<meta charset="utf-8">
<title>${body}</title>
<style>
  body { font-family: Calibri, "Microsoft YaHei", sans-serif; font-size: 11pt; color: #1F2328; }
  h1 { font-size: 20pt; } h2 { font-size: 16pt; } h3 { font-size: 14pt; }
  h4, h5, h6 { font-size: 12pt; }
  pre { background: #F6F8FA; border: 1px solid #D0D7DE; padding: 8pt; font-family: Consolas, monospace; font-size: 9.5pt; white-space: pre-wrap; word-break: break-all; }
  code { font-family: Consolas, monospace; background: #F2F4F7; padding: 0 3px; font-size: 10pt; }
  pre code { background: none; padding: 0; }
  table { border-collapse: collapse; width: 100%; }
  th, td { border: 1px solid #D0D7DE; padding: 4pt 8pt; text-align: left; vertical-align: top; }
  th { background: #F6F8FA; font-weight: bold; }
  blockquote { border-left: 3px solid #D0D7DE; padding-left: 10pt; color: #57606A; margin-left: 0; }
  img { max-width: 100%; }
</style>
</head>
<body>${clone.innerHTML}</body>
</html>`
}

function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
}

async function exportWord() {
  const content = document.querySelector('.rp-doc, .rp-doc-layout__overview')
  if (!content) {
    alert('当前页面没有可导出的正文')
    return
  }
  try {
    const { default: htmlDocx } = await import('html-docx-js/dist/html-docx')
    const title = docTitle()
    const blob = htmlDocx.asBlob(buildWordHtml(content, title), {
      orientation: 'portrait',
      margins: { top: 1440, right: 1440, bottom: 1440, left: 1440 },
    })
    downloadBlob(blob, `${safeName(title)}.docx`)
  } catch (e) {
    console.error('[export] word export failed:', e)
    alert('Word 导出失败，请改用"导出 PDF"（浏览器打印）')
  }
}

const ITEMS = [
  { label: '导出 Markdown', hint: '下载页面源文件 .md', action: exportMarkdown },
  { label: '导出 PDF', hint: '通过浏览器打印生成', action: exportPdf },
  { label: '导出 Word', hint: '正文转换为 .docx', action: exportWord },
]

export function ExportMenu() {
  const [open, setOpen] = useState(false)
  const rootRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!open) return
    const onDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setOpen(false)
    }
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setOpen(false)
    }
    document.addEventListener('mousedown', onDown)
    document.addEventListener('keydown', onKey)
    return () => {
      document.removeEventListener('mousedown', onDown)
      document.removeEventListener('keydown', onKey)
    }
  }, [open])

  return (
    <div className="export-menu" ref={rootRef}>
      <button
        type="button"
        className="export-menu__btn"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => setOpen(v => !v)}
      >
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
          <polyline points="7 10 12 15 17 10" />
          <line x1="12" y1="15" x2="12" y2="3" />
        </svg>
        <span>导出</span>
      </button>
      {open && (
        <div className="export-menu__dropdown" role="menu">
          {ITEMS.map(item => (
            <button
              key={item.label}
              type="button"
              role="menuitem"
              className="export-menu__item"
              onClick={() => {
                setOpen(false)
                void item.action()
              }}
            >
              <span className="export-menu__item-label">{item.label}</span>
              <span className="export-menu__item-hint">{item.hint}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  )
}
