# 数据库 ORM 与迁移

`db/models/` 使用 SQLAlchemy 2 的 `DeclarativeBase`、`Mapped`、`mapped_column` 定义现有
19 张业务表。四遥对应 `YcPoint` / `YxPoint` / `YkPoint` / `YtPoint`，共享抽象 `PointBase`，
仍是四张物理表。Alembic 管理数据库结构的初始化和迁移，业务进程不在启动时执行 DDL。

19 张业务表统一具有数据库生成的整数 `id` 主键。设备类型、类型固有点、模板、导入任务、
通信统计和历史表的原业务主键改为唯一约束；外键继续引用这些稳定业务字段。
`alembic_version` 是 Alembic 自己维护的迁移记录表，不属于业务模型。

## 安装与配置

在仓库根目录执行（Python 3.11+）：

```powershell
python -m pip install -e ./backend/python
# 按需安装驱动；SQLite 使用 Python 自带驱动。
python -m pip install -e "./backend/python[mysql]"
python -m pip install -e "./backend/python[postgresql]"
```

使用 uv 时，在 `backend/python` 中按所选数据库安装依赖：

```powershell
uv sync --extra mysql          # MySQL，包含 PyMySQL 与认证依赖
# 或 uv sync --extra postgresql
ems-db init
```

仅运行 `uv sync` 不会安装可选数据库驱动；切换数据库后需同步对应 extra。
SQLite 不需要额外驱动。安装命令需在运行 `ems-db` 的同一虚拟环境中执行。

编辑本目录的 [`database.yaml`](database.yaml)。它**仅供 Python 数据库迁移工具使用**，
不是全局配置，也不改变 C++ 进程的数据库配置。

```yaml
database: postgresql
databases:
  sqlite:
    url: sqlite:///./ems.sqlite3
  mysql:
    url: mysql+pymysql://
    host: 127.0.0.1
    port: 3306
    username: ems
    password: ""
    database: ems
  postgresql:
    url: postgresql+psycopg://
    host: 127.0.0.1
    port: 5432
    username: ems
    password: ""
    database: ems
```

`database` 选择使用哪一组配置；SQLite 不需要账号和端口。`url` 也可以填完整连接 URL，
同组显式填写的 `host` / `port` / `username` / `password` / `database` 覆盖 URL 对应字段。
密码作为单独字段填写时无需 URL 转义。相对 SQLite 文件路径相对 **YAML 所在目录**解析，
默认文件是 `backend/python/ems.sqlite3`，已加入 Git 忽略规则。

默认配置位置固定为 `backend/python/database.yaml`，与运行命令时的工作目录无关。
用 `ems-db --config /path/to/database.yaml ...` 指定其他文件。配置优先级：
`EMS_DATABASE_URL` > `--config` > `EMS_DATABASE_CONFIG` > 本目录默认 YAML。
选中 MySQL 时，`EMS_MYSQL_*` 可以覆盖该组连接字段；仍兼容原来的 MySQL JSON 文件。
实际密码建议通过环境变量或本地独立配置提供，仓库模板保持空密码。

## 初始化与常用命令

先创建空 MySQL / PostgreSQL 数据库并授予部署账号 DDL 权限；SQLite 自动创建文件，父目录需存在。
修改 YAML 选定数据库后：

```powershell
ems-db init                   # 升级结构到 head，不自动写业务数据
ems-db seed catalog           # 显式初始化站根、11 个类型、47 个固有点
ems-db seed demo              # 可选开发演示数据
ems-db current
ems-db history
ems-db check                  # 比对 ORM 与当前库；差异返回非零退出码
```

目录种子重复执行更新指定目录字段，保留已有站根；演示种子以业务编码查找，保留已有数据，
并使用数据库生成的 ID，避免 PostgreSQL 序列与固定演示 ID 冲突。部署命令应单点串行运行。
种子使用 ORM `Session`，由调用方统一提交或回滚，不使用数据库专属的 UPSERT SQL。

安装后可以在任意工作目录调用 `ems-db`。在本目录中也可以使用 `python -m db ...`。

## 修改结构与迁移

表结构只在 `db/models/` 的 ORM 模型中定义，先修改模型，再让 Alembic 生成迁移：

```powershell
ems-db revision --autogenerate -m "add device field"
# 审查生成文件，补齐必要的数据转换、CHECK、枚举和函数索引操作。
ems-db upgrade head
ems-db check
ems-db downgrade <revision>
```

SQLite 自动生成迁移采用 Alembic batch 模式以支持表重建。初始迁移保存固定的表定义，
不在执行时导入当前 ORM 元数据或读取会变化的旧 SQL 文件。后续迁移采用 Alembic revision ID，
不增加 `004_*.sql` / `*_v2.*`。当前初始迁移已从 ORM 模型重新生成，包含全部业务表的 `id`。
`upgrade head` 不会重放同一 revision；后续模型变更仍需先生成新的 Alembic 迁移。

迁移工具对 SQLite 显式开启 DDL 事务。重建表时在事务外暂停外键约束，避免删除旧表触发级联；
提交前执行外键检查，失败则回滚，最后恢复外键约束。包含函数索引的表在 batch 重建时仍需在
迁移脚本中显式保留或重建这些索引，不能依赖 SQLite 反射自动恢复。

`alembic_version` 只记录**数据库结构迁移位置**；不引入配置草稿、发布、快照或配置版本表。
设备配置仍保存即生效，热加载仍依赖站根 `cfg_device.config_revision`。

不连接数据库也能检查某个后端的初始化 SQL：

```powershell
ems-db upgrade head --sql --dialect sqlite
ems-db upgrade head --sql --dialect mysql
ems-db upgrade head --sql --dialect postgresql
```

`--autogenerate` 是候选迁移生成工具。表达式索引、枚举 CHECK、复杂约束以及重命名需要人工审查；
`check` 比对表、字段、默认值、常规索引、外键和主键，并核对预期函数索引是否存在，
不保证跨方言的全部 CHECK 和函数索引表达式等价。

既有数据库先执行 `ems-db check` 并人工核验 CHECK 与函数索引。
只有表结构已与当前模型一致，才能使用
`alembic -c backend/python/db/alembic.ini stamp c7b19f2a6d40` 登记当前初始迁移。
重置过迁移历史的开发库也必须先确认结构一致，再重新登记；这不会执行建表或修改业务数据。
旧库如果缺少 `id` 或其他字段，不能用 `stamp` 冒充已迁移，应先按模型差异生成并审查迁移。
`stamp` 只登记位置，不执行表结构变更，也不验证兼容性。

## ORM 使用与兼容范围

```python
from sqlalchemy import select
from db.config import load_url, make_engine, session_factory
from db.models import Device

engine = make_engine(load_url())
try:
    with session_factory(engine).begin() as session:
        stations = session.scalars(select(Device).where(Device.node_kind == "station")).all()
finally:
    engine.dispose()
```

三种数据库共用 ORM 字段、约束及 ORM 种子逻辑。SQLite 自增主键使用 `INTEGER`；
PostgreSQL 使用自己的序列与类型；MySQL 通过类型 variant 保留原有 unsigned、ENUM、
微秒时间戳和 BLOB 兼容。跨库整数业务值应限制在有符号 64 位范围内，SQLite 的 NUMERIC
精度和时间精度不等同于服务端数据库。`updated_at` 在 ORM 更新时维护，MySQL 还保留服务端
`ON UPDATE`；SQLite / PostgreSQL 的外部原生 SQL 更新不会自动改该字段。

设备配置只保留 `cfg_device`、`cfg_channel`、`cfg_binding` 和四张 `cfg_point_*` 表。
旧的 `device`、`channel`、`point_yc/yx/yk/yt` ORM 模型和建表定义已删除；采集进程读取同一套新表。
开发演示数据也写入新表，设备和通道默认禁用，配置真实端点后再启用。
开发阶段直接更新初始迁移；已经执行过旧基线的数据库不会因为再次 `init` 自动删除旧表，
`ems-db check` 会报告这些退役表；通过 `ems-db revision --autogenerate -m "remove legacy tables"`
可从当前 ORM 自动生成删除变更，审查后再 `ems-db upgrade head`。不能用 `stamp` 代替结构变更。

本工具覆盖现有 19 张关系表，表结构与种子数据分别维护在 `db/models/` 和 `db/seed_data.py`。
原 `backend/data` 下的 SQL 脚本已移除。TDengine 历史库不在当前 SQLAlchemy 迁移范围内，
需根据[存储设计](../../website/docs/processes/storage/design.md#31-表结构与时间)由部署单独准备。
现有 C++ 运行时仍通过 MySQL/ormpp 访问数据库；本次支持三库的是 Python ORM 与迁移工具，
切换 C++ 运行时的驱动需要另行改造。

## 验证

```powershell
python -m pip install -e "./backend/python[test]"
python -m pytest backend/python/db/tests
```

测试包含 SQLite 实际建表、ORM 读写、外键/CHECK/级联、种子幂等/回滚、迁移升降级、
batch 重建时的关联数据保留和 DDL 回滚，
以及三个方言下固定迁移与 ORM 建表 DDL 的一致性。MySQL / PostgreSQL 的 DDL 编译检查
不等同于真实服务器联调。

实现参考 [SQLAlchemy 声明式映射](https://docs.sqlalchemy.org/en/20/orm/declarative_tables.html)
和 [Alembic 自动迁移](https://alembic.sqlalchemy.org/en/latest/autogenerate.html)。
