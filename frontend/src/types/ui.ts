/** 下划线页签条目（后台分栏导航 / 前台数据页签共用） */
export interface TabItem {
  key: string
  label: string
  /** 点位数量等辅助数字，显示在标签右侧 */
  count?: number
}

/** 折线图序列：缺测点用 null 表示，曲线在此断开 */
export interface ChartSeries {
  name: string
  /** CSS 颜色，一般取设计令牌 var(--c-accent) */
  color: string
  values: (number | null)[]
}

/** 顶栏用户菜单项（画板 17 后台管理 / 画板 18 返回前台） */
export interface UserMenuItem {
  key: string
  label: string
  /** lucide 图标名 */
  icon: string
  /** 主入口用 accent，退出等破坏性操作用 danger */
  tone?: 'accent' | 'danger'
}
