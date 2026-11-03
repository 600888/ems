import { cpSync, mkdirSync, readdirSync, rmdirSync, statSync, unlinkSync } from 'node:fs'
import path from 'node:path'

console.log('P0: module loaded')
const docsDir = path.resolve(import.meta.dirname, '..', 'docs')
const outDir = path.join(docsDir, 'public', 'raw')
console.log('P1: dirs', docsDir, outDir)

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

const sources = new Map()
for (const name of readdirSync(docsDir)) {
  const abs = path.join(docsDir, name)
  if (statSync(abs).isDirectory()) {
    if (name === 'public') continue
    collect(abs, name, sources)
  }
}
console.log('P2: collected sources (all files):', sources.size)

const targets = new Map()
if (statSync(outDir).isDirectory()) {
  for (const [rel, abs] of collect(outDir)) {
    if (/\.(md|mdx)$/.test(rel)) targets.set(rel, abs)
  }
}
console.log('P3: targets:', targets.size)

let copied = 0
for (const [rel, src] of sources) {
  if (!/\.(md|mdx)$/.test(rel)) continue
  const dest = path.join(outDir, rel)
  mkdirSync(path.dirname(dest), { recursive: true })
  cpSync(src, dest)
  copied++
}
console.log('P4: copied', copied)
console.log('P5: done')
