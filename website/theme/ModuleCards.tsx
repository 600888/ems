import { Link } from '@rspress/core/theme-original';

const modules = [
  {
    label: '架构与业务模型',
    en: 'ARCHITECTURE',
    href: '/architecture/',
    description: '从系统边界开始，理解设备模型、点表绑定与通道协议。',
    tags: '系统架构 · 设备模型 · 点表导入',
    icon: 'grid',
  },
  {
    label: '进程设计',
    en: 'PROCESSES',
    href: '/processes/',
    description: '采集与上送、实时数据库、存储、策略和 Web，各进程的职责与协作方式。',
    tags: '采集 · 实时数据库 · 存储 · 策略 · Web',
    icon: 'network',
  },
  {
    label: '公共模块',
    en: 'COMMON MODULES',
    href: '/common/',
    description: '共享模型、实时数据访问、Redis 键与消息交接的公共契约。',
    tags: 'Realtime · Redis · ZeroMQ',
    icon: 'grid',
  },
  {
    label: '技术决策',
    en: 'DESIGN DECISIONS',
    href: '/decisions/',
    description: '了解技术选型的前提、取舍，以及实施前需要验证的条件。',
    tags: 'Drogon · 消息订阅 · 可靠性',
    icon: 'book',
  },
  {
    label: '开发参考',
    en: 'REFERENCE',
    href: '/reference/',
    description: '统一协议处理器、动态库配置与 C/C++ 代码格式规范。',
    tags: 'Modbus · IEC 104 · DLT645 · DLT698',
    icon: 'network',
  },
  {
    label: '文档维护',
    en: 'DOCUMENTATION',
    href: '/guide/',
    description: '本地启动文档站点，维护页面、导航、图表与构建产物。',
    tags: 'Rspress · Markdown · Mermaid',
    icon: 'book',
  },
];

function ModuleIcon({ name }: { name: string }) {
  return (
    <svg viewBox="0 0 24 24" width="25" height="25" fill="none" stroke="currentColor"
      strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      {name === 'grid' ? <><rect x="4" y="4" width="16" height="16" rx="2.5" />
        <path d="M4 10h16M10 4v16" /></> : name === 'network' ? <>
        <circle cx="12" cy="12" r="9" /><ellipse cx="12" cy="12" rx="4" ry="9" />
        <path d="M3 12h18M5 6h14M5 18h14" /></> : <>
        <path d="M4 5.5A2.5 2.5 0 0 1 6.5 3H19v15H6.5A2.5 2.5 0 0 0 4 20.5V5.5Z" />
        <path d="M4 20.5A2.5 2.5 0 0 1 6.5 18H19v3H6.5" /></>}
    </svg>
  );
}

export function ModuleCards() {
  return (
    <div className="module-grid">
      {modules.map((module, index) => (
        <Link key={module.href} href={module.href} className="module-card">
          <div className="module-card-top">
            <span className="module-icon"><ModuleIcon name={module.icon} /></span>
            <span className="module-number">{String(index + 1).padStart(2, '0')}</span>
          </div>
          <span className="module-en">{module.en}</span>
          <strong className="module-title">{module.label}<span aria-hidden="true">↗</span></strong>
          <span className="module-description">{module.description}</span>
          <span className="module-tags">{module.tags}</span>
        </Link>
      ))}
    </div>
  );
}
