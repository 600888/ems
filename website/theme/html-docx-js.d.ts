/**
 * html-docx-js 浏览器版（browserify 产物）的类型声明。
 * 注意：不要直接 import 'html-docx-js'（main 入口引用 fs，仅限 Node）。
 */
declare module 'html-docx-js/dist/html-docx' {
  export interface DocxMargins {
    top?: number
    right?: number
    bottom?: number
    left?: number
    header?: number
    footer?: number
    gutter?: number
  }

  export interface AsBlobOptions {
    orientation?: 'portrait' | 'landscape'
    margins?: DocxMargins
  }

  export function asBlob(html: string, options?: AsBlobOptions): Blob

  const htmlDocx: { asBlob: typeof asBlob }
  export default htmlDocx
}
