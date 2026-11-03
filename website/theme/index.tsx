import './index.css'
import { Layout as OriginalLayout, type LayoutProps } from '@rspress/core/theme-original'
import { ExportMenu } from './ExportMenu'

export * from '@rspress/core/theme-original'

// 需要显示文档导出入口时改为 true。
const ENABLE_DOCUMENT_EXPORT = false

/** 将导出操作放在当前文档内容顶部，随文档滚动。 */
export function Layout(props: LayoutProps) {
  return (
    <OriginalLayout
      {...props}
      beforeDocContent={
        <>
          {ENABLE_DOCUMENT_EXPORT && (
            <div className="doc-actions" data-export-ignore>
              <ExportMenu />
            </div>
          )}
          {props.beforeDocContent}
        </>
      }
    />
  )
}
