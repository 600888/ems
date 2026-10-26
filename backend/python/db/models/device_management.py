"""Declarative ORM models for device_management."""

from .point import PointBase
from datetime import datetime
from decimal import Decimal
from typing import Any
import sqlalchemy as sa
from sqlalchemy.orm import Mapped, mapped_column
from ..types import enumeration, integer, timestamp, TimestampDefault
from . import Base


class Device(Base):
    """cfg_device persisted rows."""

    __tablename__ = "cfg_device"
    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    device_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="稳定设备 ID，全树唯一"
    )
    parent_device_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''"), comment="父节点；站根为空串"
    )
    node_kind: Mapped[str] = mapped_column(
        enumeration("station", "unit", "device", "component", name="ck_cfg_device_node_kind"),
        nullable=False,
    )
    code: Mapped[str] = mapped_column(sa.String(64), nullable=False, comment="同父节点下唯一")
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    path: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, comment="相对站根的路径，如 /unit01/battery01/rack01"
    )
    depth: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="0-8",
    )
    sort_order: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    device_type_code: Mapped[str] = mapped_column(
        sa.String(32), nullable=False, server_default=sa.text("''"), comment="部件节点同样引用类型"
    )
    type_version: Mapped[str] = mapped_column(
        sa.String(16), nullable=False, server_default=sa.text("''")
    )
    template_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''"), comment="创建时套用的型号模板"
    )
    source_device_id: Mapped[str] = mapped_column(
        sa.String(64),
        nullable=False,
        server_default=sa.text("''"),
        comment="component 必填：采集它的通信设备",
    )
    vendor: Mapped[str] = mapped_column(sa.String(64), nullable=False, server_default=sa.text("''"))
    model: Mapped[str] = mapped_column(sa.String(64), nullable=False, server_default=sa.text("''"))
    firmware_version: Mapped[str] = mapped_column(
        sa.String(32), nullable=False, server_default=sa.text("''")
    )
    rated_power_kw: Mapped[Decimal | None] = mapped_column(sa.Numeric(12, 3), nullable=True)
    timezone: Mapped[str] = mapped_column(
        sa.String(64),
        nullable=False,
        server_default=sa.text("'Asia/Shanghai'"),
        comment="仅站根使用",
    )
    default_stale_after_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("60000"),
        comment="仅站根使用；0 表示不判陈旧",
    )
    config_revision: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="仅站根使用：每次保存 +1，供热加载轮询",
    )
    enabled: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
        comment="配置启用，与运行在线状态无关",
    )
    description: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    legacy_ref: Mapped[str | None] = mapped_column(
        sa.String(64), nullable=True, comment="旧四遥表数字 ID"
    )
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    updated_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=True, microseconds=True),
        onupdate=sa.func.current_timestamp(),
    )
    __table_args__ = (
        sa.CheckConstraint("depth <= 8", name="ck_device_depth"),
        sa.CheckConstraint(
            " node_kind <> 'component' OR source_device_id <> '' ", name="ck_device_component"
        ),
        sa.CheckConstraint(
            " node_kind IN ('station', 'unit') OR (device_type_code <> '' AND type_version <> '') ",
            name="ck_device_device_type",
        ),
        sa.Index("uq_device_id", "device_id", unique=True),
        sa.Index("uq_device_path", "path", unique=True),
        sa.Index("uq_device_sibling", "parent_device_id", "code", unique=True),
        sa.Index("ix_device_parent", "parent_device_id", "sort_order"),
        sa.Index("ix_device_kind", "node_kind", "enabled"),
        sa.Index("ix_device_source", "source_device_id"),
        sa.Index("ix_device_type", "device_type_code"),
        {
            "comment": "设备树（站/单元/设备/部件）",
            "mysql_engine": "InnoDB",
            "mysql_charset": "utf8mb4",
        },
    )


class DeviceType(Base):
    """cfg_device_type persisted rows."""

    __tablename__ = "cfg_device_type"
    type_code: Mapped[str] = mapped_column(sa.String(32), nullable=False, primary_key=True)
    type_version: Mapped[str] = mapped_column(sa.String(16), nullable=False, primary_key=True)
    name: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    category: Mapped[str] = mapped_column(
        sa.String(32),
        nullable=False,
        server_default=sa.text("'device'"),
        comment="pcs/bms/hvac/env/fire/meter/relay/battery/rack/pack/cell",
    )
    status: Mapped[str] = mapped_column(
        enumeration("active", "retired", name="ck_cfg_device_type_status"),
        nullable=False,
        server_default=sa.text("'active'"),
    )
    description: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    updated_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=True, microseconds=True),
        onupdate=sa.func.current_timestamp(),
    )
    __table_args__ = (
        {"comment": "设备类型（含类型版本）", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class TypePoint(Base):
    """cfg_type_point persisted rows."""

    __tablename__ = "cfg_type_point"
    type_code: Mapped[str] = mapped_column(sa.String(32), nullable=False, primary_key=True)
    type_version: Mapped[str] = mapped_column(sa.String(16), nullable=False, primary_key=True)
    semantic_code: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="如 active_power、run_state、start", primary_key=True
    )
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    point_type: Mapped[str] = mapped_column(
        enumeration("yc", "yx", "yk", "yt", name="ck_cfg_type_point_point_type"),
        nullable=False,
        comment="决定映射行必须落在哪张点表",
    )
    value_type: Mapped[str] = mapped_column(
        enumeration(
            "bool", "int64", "uint64", "double", "string", name="ck_cfg_type_point_value_type"
        ),
        nullable=False,
    )
    unit: Mapped[str] = mapped_column(sa.String(32), nullable=False, server_default=sa.text("''"))
    required: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
        comment="属于任一能力即为必需",
    )
    capability_codes_json: Mapped[Any] = mapped_column(
        sa.JSON(), nullable=False, comment="所属能力编码数组，可属多个能力"
    )
    writable: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    stale_after_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="0 表示继承站默认",
    )
    sort_order: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["type_code", "type_version"],
            ["cfg_device_type.type_code", "cfg_device_type.type_version"],
            name="fk_type_point_type",
            ondelete="CASCADE",
        ),
        sa.CheckConstraint(
            "point_type IN ('yk', 'yt') OR writable = 0", name="ck_type_point_writable"
        ),
        sa.Index("ix_type_point_cap", "type_code", "type_version"),
        {
            "comment": "设备类型固有点目录（四遥）",
            "mysql_engine": "InnoDB",
            "mysql_charset": "utf8mb4",
        },
    )


class DeviceTemplate(Base):
    """cfg_template persisted rows."""

    __tablename__ = "cfg_template"
    template_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="如 PCS-125K", primary_key=True
    )
    template_version: Mapped[str] = mapped_column(
        sa.String(16), nullable=False, comment="如 v2.3", primary_key=True
    )
    device_type_code: Mapped[str] = mapped_column(sa.String(32), nullable=False)
    type_version: Mapped[str] = mapped_column(sa.String(16), nullable=False)
    vendor: Mapped[str] = mapped_column(sa.String(64), nullable=False, server_default=sa.text("''"))
    model: Mapped[str] = mapped_column(sa.String(64), nullable=False, server_default=sa.text("''"))
    status: Mapped[str] = mapped_column(
        enumeration("active", "retired", name="ck_cfg_template_status"),
        nullable=False,
        server_default=sa.text("'active'"),
    )
    points_json: Mapped[Any] = mapped_column(
        sa.JSON(), nullable=False, comment="默认四遥点表与映射，复制源，不参与运行期联表"
    )
    description: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    updated_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=True, microseconds=True),
        onupdate=sa.func.current_timestamp(),
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["device_type_code", "type_version"],
            ["cfg_device_type.type_code", "cfg_device_type.type_version"],
            name="fk_template_type",
        ),
        sa.Index("ix_template_type", "device_type_code", "type_version"),
        {"comment": "型号模板", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class Channel(Base):
    """cfg_channel persisted rows."""

    __tablename__ = "cfg_channel"
    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    channel_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    code: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    protocol: Mapped[str] = mapped_column(
        sa.String(32), nullable=False, comment="modbus/iec104/dlt645/iec61850"
    )
    conn_mode: Mapped[str] = mapped_column(
        enumeration("tcp_client", "tcp_server", "serial", "udp", name="ck_cfg_channel_conn_mode"),
        nullable=False,
    )
    endpoint: Mapped[str] = mapped_column(
        sa.String(160), nullable=False, server_default=sa.text("''"), comment="host:port 或 COM3"
    )
    serial_json: Mapped[Any | None] = mapped_column(
        sa.JSON(), nullable=True, comment="波特率/数据位/停止位/校验"
    )
    poll_interval_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1000"),
    )
    timeout_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("3000"),
    )
    retry_times: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    max_sessions: Mapped[int] = mapped_column(
        integer(size="small", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
        comment="允许并发的绑定数，表达共享连接",
    )
    enabled: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    description: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    updated_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=True, microseconds=True),
        onupdate=sa.func.current_timestamp(),
    )
    __table_args__ = (
        sa.CheckConstraint("poll_interval_ms > 0 AND timeout_ms > 0", name="ck_channel_timing"),
        sa.Index("uq_channel_id", "channel_id", unique=True),
        sa.Index("uq_channel_code", "code", unique=True),
        {
            "comment": "通道（一条通道即一条物理连接）",
            "mysql_engine": "InnoDB",
            "mysql_charset": "utf8mb4",
        },
    )


class Binding(Base):
    """cfg_binding persisted rows."""

    __tablename__ = "cfg_binding"
    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    binding_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    device_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    channel_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    device_address: Mapped[str] = mapped_column(
        sa.String(64),
        nullable=False,
        server_default=sa.text("''"),
        comment="Unit ID / 公共地址 / 从站号",
    )
    profile_version: Mapped[str] = mapped_column(
        sa.String(32), nullable=False, server_default=sa.text("''"), comment="如 mbtcp-v3"
    )
    can_collect: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    can_control: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    can_forward: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    enabled: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    description: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    updated_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=True, microseconds=True),
        onupdate=sa.func.current_timestamp(),
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["device_id"], ["cfg_device.device_id"], name="fk_binding_device", ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(
            ["channel_id"],
            ["cfg_channel.channel_id"],
            name="fk_binding_channel",
            ondelete="CASCADE",
        ),
        sa.CheckConstraint("can_collect + can_control + can_forward > 0", name="ck_binding_role"),
        sa.Index("uq_binding_id", "binding_id", unique=True),
        sa.Index("uq_binding_pair", "device_id", "channel_id", unique=True),
        sa.Index("ix_binding_channel", "channel_id"),
        {
            "comment": "设备-通道绑定与角色标志",
            "mysql_engine": "InnoDB",
            "mysql_charset": "utf8mb4",
        },
    )


class YcPoint(PointBase):
    """cfg_point_yc persisted rows."""

    __tablename__ = "cfg_point_yc"
    origin: Mapped[str] = mapped_column(
        enumeration("type", "template", "vendor", "component", name="ck_cfg_point_yc_origin"),
        nullable=False,
        server_default=sa.text("'vendor'"),
    )
    address_space: Mapped[str | None] = mapped_column(
        enumeration(
            "coil",
            "discrete_input",
            "input_register",
            "holding_register",
            name="ck_cfg_point_yc_address_space",
        ),
        nullable=True,
        comment="空=非寄存器协议",
    )
    wire_type: Mapped[str] = mapped_column(
        enumeration(
            "bool",
            "i16",
            "u16",
            "i32",
            "u32",
            "f32",
            "i64",
            "u64",
            "f64",
            name="ck_cfg_point_yc_wire_type",
        ),
        nullable=False,
    )
    byte_order: Mapped[str | None] = mapped_column(
        enumeration("ab", "ba", name="ck_cfg_point_yc_byte_order"),
        nullable=True,
        comment="空=单寄存器",
    )
    value_type: Mapped[str] = mapped_column(
        enumeration("double", "int64", "uint64", name="ck_cfg_point_yc_value_type"),
        nullable=False,
        server_default=sa.text("'double'"),
    )
    unit: Mapped[str] = mapped_column(sa.String(32), nullable=False, server_default=sa.text("''"))
    decimals: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="0 表示按值类型默认",
    )
    scale_factor: Mapped[Decimal] = mapped_column(
        sa.Numeric(20, 8), nullable=False, server_default=sa.text("1")
    )
    offset_value: Mapped[Decimal] = mapped_column(
        sa.Numeric(20, 8), nullable=False, server_default=sa.text("0")
    )
    range_min: Mapped[float] = mapped_column(
        sa.Double(), nullable=False, server_default=sa.text("-9999999")
    )
    range_max: Mapped[float] = mapped_column(
        sa.Double(), nullable=False, server_default=sa.text("9999999")
    )
    deadband: Mapped[float] = mapped_column(
        sa.Double(), nullable=False, server_default=sa.text("0"), comment="变化死区，0 表示逐点上报"
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["owner_device_id"], ["cfg_device.device_id"], name="fk_yc_device", ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(
            ["binding_id"], ["cfg_binding.binding_id"], name="fk_yc_binding", ondelete="CASCADE"
        ),
        sa.CheckConstraint("scale_factor <> 0", name="ck_yc_scale"),
        sa.CheckConstraint("range_max >= range_min", name="ck_yc_range"),
        sa.CheckConstraint("enabled = 1 OR length(disabled_reason) > 0", name="ck_yc_reason"),
        sa.Index("uq_yc_point_id", "point_id", unique=True),
        sa.Index("uq_yc_code", "owner_device_id", "code", unique=True),
        sa.Index(
            "uq_yc_semantic", "owner_device_id", sa.text("(NULLIF(semantic_code, ''))"), unique=True
        ),
        sa.Index(
            "uq_yc_vendor_key", "binding_id", sa.text("(NULLIF(vendor_key, ''))"), unique=True
        ),
        sa.Index("ix_yc_owner", "owner_device_id", "code"),
        sa.Index("ix_yc_binding", "binding_id"),
        sa.Index("ix_yc_address", "binding_id", "address_space", "address_offset"),
        {"comment": "遥测点（YC）", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class YxPoint(PointBase):
    """cfg_point_yx persisted rows."""

    __tablename__ = "cfg_point_yx"
    origin: Mapped[str] = mapped_column(
        enumeration("type", "template", "vendor", "component", name="ck_cfg_point_yx_origin"),
        nullable=False,
        server_default=sa.text("'vendor'"),
    )
    address_space: Mapped[str | None] = mapped_column(
        enumeration(
            "coil",
            "discrete_input",
            "input_register",
            "holding_register",
            name="ck_cfg_point_yx_address_space",
        ),
        nullable=True,
    )
    wire_type: Mapped[str] = mapped_column(
        enumeration(
            "bool",
            "i16",
            "u16",
            "i32",
            "u32",
            "f32",
            "i64",
            "u64",
            "f64",
            name="ck_cfg_point_yx_wire_type",
        ),
        nullable=False,
    )
    byte_order: Mapped[str | None] = mapped_column(
        enumeration("ab", "ba", name="ck_cfg_point_yx_byte_order"), nullable=True
    )
    value_type: Mapped[str] = mapped_column(
        enumeration("bool", "int64", "string", name="ck_cfg_point_yx_value_type"),
        nullable=False,
        server_default=sa.text("'bool'"),
    )
    unit: Mapped[str] = mapped_column(sa.String(32), nullable=False, server_default=sa.text("''"))
    soe: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="记录变位；事件不是第五类遥，仍属遥信",
    )
    invert: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="原始位取反（常闭接点）",
    )
    enum_json: Mapped[Any | None] = mapped_column(
        sa.JSON(), nullable=True, comment="[{raw,label,severity}]"
    )
    unknown_policy: Mapped[str] = mapped_column(
        enumeration("retain_raw", "uncertain", "invalid", name="ck_cfg_point_yx_unknown_policy"),
        nullable=False,
        server_default=sa.text("'retain_raw'"),
    )
    feedback_of_point_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''"), comment="反馈点指回遥控/遥调点"
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["owner_device_id"], ["cfg_device.device_id"], name="fk_yx_device", ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(
            ["binding_id"], ["cfg_binding.binding_id"], name="fk_yx_binding", ondelete="CASCADE"
        ),
        sa.CheckConstraint("enabled = 1 OR length(disabled_reason) > 0", name="ck_yx_reason"),
        sa.Index("uq_yx_point_id", "point_id", unique=True),
        sa.Index("uq_yx_code", "owner_device_id", "code", unique=True),
        sa.Index(
            "uq_yx_semantic", "owner_device_id", sa.text("(NULLIF(semantic_code, ''))"), unique=True
        ),
        sa.Index(
            "uq_yx_vendor_key", "binding_id", sa.text("(NULLIF(vendor_key, ''))"), unique=True
        ),
        sa.Index("ix_yx_owner", "owner_device_id", "code"),
        sa.Index("ix_yx_binding", "binding_id"),
        sa.Index("ix_yx_address", "binding_id", "address_space", "address_offset"),
        sa.Index("ix_yx_feedback", "feedback_of_point_id"),
        {"comment": "遥信点（YX）", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class YkPoint(PointBase):
    """cfg_point_yk persisted rows."""

    __tablename__ = "cfg_point_yk"
    origin: Mapped[str] = mapped_column(
        enumeration("type", "template", "vendor", "component", name="ck_cfg_point_yk_origin"),
        nullable=False,
        server_default=sa.text("'vendor'"),
    )
    address_space: Mapped[str | None] = mapped_column(
        enumeration(
            "coil",
            "discrete_input",
            "input_register",
            "holding_register",
            name="ck_cfg_point_yk_address_space",
        ),
        nullable=True,
    )
    wire_type: Mapped[str] = mapped_column(
        enumeration(
            "bool",
            "i16",
            "u16",
            "i32",
            "u32",
            "f32",
            "i64",
            "u64",
            "f64",
            name="ck_cfg_point_yk_wire_type",
        ),
        nullable=False,
    )
    byte_order: Mapped[str | None] = mapped_column(
        enumeration("ab", "ba", name="ck_cfg_point_yk_byte_order"), nullable=True
    )
    value_type: Mapped[str] = mapped_column(
        enumeration("bool", "int64", name="ck_cfg_point_yk_value_type"),
        nullable=False,
        server_default=sa.text("'bool'"),
    )
    unit: Mapped[str] = mapped_column(sa.String(32), nullable=False, server_default=sa.text("''"))
    write_mode: Mapped[str] = mapped_column(
        enumeration("fixed", "direct", name="ck_cfg_point_yk_write_mode"),
        nullable=False,
        server_default=sa.text("'direct'"),
    )
    fixed_raw_value: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="fixed 模式必写",
    )
    pulse_width_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="0 表示保持",
    )
    min_interval_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    write_group_key: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''"), comment="同组写寄存器互斥"
    )
    feedback_json: Mapped[Any | None] = mapped_column(
        sa.JSON(),
        nullable=True,
        comment="[{point_code,criterion,expected,tolerance,order,required}]",
    )
    feedback_timeout_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("5000"),
    )
    failure_policy: Mapped[str] = mapped_column(
        enumeration("reject", "retry", "escalate", name="ck_cfg_point_yk_failure_policy"),
        nullable=False,
        server_default=sa.text("'reject'"),
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["owner_device_id"], ["cfg_device.device_id"], name="fk_yk_device", ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(
            ["binding_id"], ["cfg_binding.binding_id"], name="fk_yk_binding", ondelete="CASCADE"
        ),
        sa.CheckConstraint("enabled = 1 OR length(disabled_reason) > 0", name="ck_yk_reason"),
        sa.Index("uq_yk_point_id", "point_id", unique=True),
        sa.Index("uq_yk_code", "owner_device_id", "code", unique=True),
        sa.Index(
            "uq_yk_semantic", "owner_device_id", sa.text("(NULLIF(semantic_code, ''))"), unique=True
        ),
        sa.Index(
            "uq_yk_vendor_key", "binding_id", sa.text("(NULLIF(vendor_key, ''))"), unique=True
        ),
        sa.Index("ix_yk_owner", "owner_device_id", "code"),
        sa.Index("ix_yk_binding", "binding_id"),
        sa.Index("ix_yk_address", "binding_id", "address_space", "address_offset"),
        sa.Index("ix_yk_group", "binding_id", "write_group_key"),
        {"comment": "遥控点（YK）", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class YtPoint(PointBase):
    """cfg_point_yt persisted rows."""

    __tablename__ = "cfg_point_yt"
    origin: Mapped[str] = mapped_column(
        enumeration("type", "template", "vendor", "component", name="ck_cfg_point_yt_origin"),
        nullable=False,
        server_default=sa.text("'vendor'"),
    )
    address_space: Mapped[str | None] = mapped_column(
        enumeration(
            "coil",
            "discrete_input",
            "input_register",
            "holding_register",
            name="ck_cfg_point_yt_address_space",
        ),
        nullable=True,
    )
    wire_type: Mapped[str] = mapped_column(
        enumeration(
            "bool",
            "i16",
            "u16",
            "i32",
            "u32",
            "f32",
            "i64",
            "u64",
            "f64",
            name="ck_cfg_point_yt_wire_type",
        ),
        nullable=False,
    )
    byte_order: Mapped[str | None] = mapped_column(
        enumeration("ab", "ba", name="ck_cfg_point_yt_byte_order"), nullable=True
    )
    value_type: Mapped[str] = mapped_column(
        enumeration("double", "int64", name="ck_cfg_point_yt_value_type"),
        nullable=False,
        server_default=sa.text("'double'"),
    )
    unit: Mapped[str] = mapped_column(sa.String(32), nullable=False, server_default=sa.text("''"))
    decimals: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    scale_factor: Mapped[Decimal] = mapped_column(
        sa.Numeric(20, 8),
        nullable=False,
        server_default=sa.text("1"),
        comment="原码 = (工程值 - offset) / scale",
    )
    offset_value: Mapped[Decimal] = mapped_column(
        sa.Numeric(20, 8), nullable=False, server_default=sa.text("0")
    )
    range_min: Mapped[float] = mapped_column(
        sa.Double(), nullable=False, server_default=sa.text("-9999999"), comment="可写工程值下限"
    )
    range_max: Mapped[float] = mapped_column(
        sa.Double(), nullable=False, server_default=sa.text("9999999"), comment="可写工程值上限"
    )
    min_interval_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    write_group_key: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    feedback_json: Mapped[Any | None] = mapped_column(sa.JSON(), nullable=True)
    feedback_timeout_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("5000"),
    )
    failure_policy: Mapped[str] = mapped_column(
        enumeration("reject", "retry", "escalate", name="ck_cfg_point_yt_failure_policy"),
        nullable=False,
        server_default=sa.text("'reject'"),
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["owner_device_id"], ["cfg_device.device_id"], name="fk_yt_device", ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(
            ["binding_id"], ["cfg_binding.binding_id"], name="fk_yt_binding", ondelete="CASCADE"
        ),
        sa.CheckConstraint("scale_factor <> 0", name="ck_yt_scale"),
        sa.CheckConstraint("range_max >= range_min", name="ck_yt_range"),
        sa.CheckConstraint("enabled = 1 OR length(disabled_reason) > 0", name="ck_yt_reason"),
        sa.Index("uq_yt_point_id", "point_id", unique=True),
        sa.Index("uq_yt_code", "owner_device_id", "code", unique=True),
        sa.Index(
            "uq_yt_semantic", "owner_device_id", sa.text("(NULLIF(semantic_code, ''))"), unique=True
        ),
        sa.Index(
            "uq_yt_vendor_key", "binding_id", sa.text("(NULLIF(vendor_key, ''))"), unique=True
        ),
        sa.Index("ix_yt_owner", "owner_device_id", "code"),
        sa.Index("ix_yt_binding", "binding_id"),
        sa.Index("ix_yt_address", "binding_id", "address_space", "address_offset"),
        sa.Index("ix_yt_group", "binding_id", "write_group_key"),
        {"comment": "遥调点（YT）", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class ImportJob(Base):
    """cfg_import_job persisted rows."""

    __tablename__ = "cfg_import_job"
    import_job_id: Mapped[str] = mapped_column(sa.String(64), nullable=False, primary_key=True)
    device_id: Mapped[str] = mapped_column(sa.String(64), nullable=False, comment="目标通信设备")
    binding_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="目标采集/下发绑定"
    )
    source_kind: Mapped[str] = mapped_column(
        enumeration(
            "vendor_csv", "vendor_xlsx", "telemetry_xlsx", name="ck_cfg_import_job_source_kind"
        ),
        nullable=False,
    )
    file_name: Mapped[str] = mapped_column(
        sa.String(255), nullable=False, server_default=sa.text("''")
    )
    file_sha256: Mapped[str] = mapped_column(sa.CHAR(64), nullable=False)
    file_bytes: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    address_base: Mapped[str] = mapped_column(
        enumeration("zero", "one", name="ck_cfg_import_job_address_base"),
        nullable=False,
        server_default=sa.text("'zero'"),
        comment="十进制地址基准",
    )
    status: Mapped[str] = mapped_column(
        enumeration("parsed", "confirmed", "applied", "failed", name="ck_cfg_import_job_status"),
        nullable=False,
        server_default=sa.text("'parsed'"),
    )
    row_total: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    row_ok: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    row_warning: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    row_error: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    idempotency_key: Mapped[str] = mapped_column(
        sa.CHAR(64), nullable=False, comment="SHA-256(device|binding|sha256)"
    )
    submitted_by: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    submitted_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    finished_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=True), nullable=True
    )
    error_code: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    error_message: Mapped[str] = mapped_column(
        sa.String(1024), nullable=False, server_default=sa.text("''")
    )
    __table_args__ = (
        sa.Index("uq_import_idem", "idempotency_key", unique=True),
        sa.Index("ix_import_target", "device_id", "submitted_at"),
        {"comment": "点表导入任务与幂等键", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class ImportRow(Base):
    """cfg_import_row persisted rows."""

    __tablename__ = "cfg_import_row"
    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    import_job_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    line_no: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False), nullable=False, autoincrement=False
    )
    vendor_key: Mapped[str] = mapped_column(
        sa.String(120), nullable=False, server_default=sa.text("''")
    )
    vendor_name: Mapped[str] = mapped_column(
        sa.String(128), nullable=False, server_default=sa.text("''")
    )
    point_type: Mapped[str] = mapped_column(
        enumeration("yc", "yx", "yk", "yt", name="ck_cfg_import_row_point_type"),
        nullable=False,
        comment="决定写入哪张点表",
    )
    target_device_path: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''"), comment="空即“目标节点缺失”"
    )
    semantic_code: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    address_space: Mapped[str | None] = mapped_column(
        enumeration(
            "coil",
            "discrete_input",
            "input_register",
            "holding_register",
            name="ck_cfg_import_row_address_space",
        ),
        nullable=True,
    )
    address_offset: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    bit_offset: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    register_count: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    protocol_address_json: Mapped[Any | None] = mapped_column(sa.JSON(), nullable=True)
    wire_type: Mapped[str] = mapped_column(sa.String(16), nullable=False)
    byte_order: Mapped[str] = mapped_column(
        sa.String(8), nullable=False, server_default=sa.text("''")
    )
    value_type: Mapped[str] = mapped_column(
        sa.String(16), nullable=False, server_default=sa.text("''")
    )
    scale_factor: Mapped[Decimal] = mapped_column(
        sa.Numeric(20, 8), nullable=False, server_default=sa.text("1")
    )
    offset_value: Mapped[Decimal] = mapped_column(
        sa.Numeric(20, 8), nullable=False, server_default=sa.text("0")
    )
    unit: Mapped[str] = mapped_column(sa.String(32), nullable=False, server_default=sa.text("''"))
    parse_status: Mapped[str] = mapped_column(
        enumeration("ok", "warning", "error", name="ck_cfg_import_row_parse_status"),
        nullable=False,
        server_default=sa.text("'ok'"),
    )
    parse_code: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    parse_message: Mapped[str] = mapped_column(
        sa.String(1024), nullable=False, server_default=sa.text("''")
    )
    confirm_state: Mapped[str] = mapped_column(
        enumeration(
            "none", "accepted", "fixed", "rejected", name="ck_cfg_import_row_confirm_state"
        ),
        nullable=False,
        server_default=sa.text("'none'"),
    )
    confirm_note: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    confirm_by: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    confirm_at: Mapped[datetime | None] = mapped_column(timestamp(microseconds=True), nullable=True)
    resolved_point_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    __table_args__ = (
        sa.ForeignKeyConstraint(
            ["import_job_id"],
            ["cfg_import_job.import_job_id"],
            name="fk_import_row_job",
            ondelete="CASCADE",
        ),
        sa.Index("uq_import_row", "import_job_id", "line_no", unique=True),
        sa.Index("ix_import_row_key", "import_job_id", "vendor_key"),
        {"comment": "导入逐行解析与逐项确认", "mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class CommStat5m(Base):
    """ops_comm_stat_5m persisted rows."""

    __tablename__ = "ops_comm_stat_5m"
    binding_id: Mapped[str] = mapped_column(sa.String(64), nullable=False, primary_key=True)
    window_start: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True), nullable=False, primary_key=True
    )
    request_total: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    success_total: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    timeout_total: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    error_total: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    avg_latency_ms: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False), nullable=True, autoincrement=False
    )
    p95_latency_ms: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False), nullable=True, autoincrement=False
    )
    last_valid_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=True), nullable=True
    )
    last_error_code: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    __table_args__ = (
        sa.Index("ix_comm_stat_window", "window_start"),
        {
            "comment": "绑定维度 5 分钟通信统计",
            "mysql_engine": "InnoDB",
            "mysql_charset": "utf8mb4",
        },
    )


class StateLog(Base):
    """ops_state_log persisted rows."""

    __tablename__ = "ops_state_log"
    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    object_kind: Mapped[str] = mapped_column(
        enumeration("device", "channel", "binding", name="ck_ops_state_log_object_kind"),
        nullable=False,
    )
    object_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    state: Mapped[str] = mapped_column(
        sa.String(32), nullable=False, comment="unconfigured/online/alarm/fault/offline"
    )
    since: Mapped[datetime] = mapped_column(timestamp(microseconds=True), nullable=False)
    reason: Mapped[str] = mapped_column(
        sa.String(255), nullable=False, server_default=sa.text("''")
    )
    __table_args__ = (
        sa.Index("ix_state_object", "object_kind", "object_id", "since"),
        sa.Index("ix_state_window", "since"),
        {
            "comment": "设备/通道/绑定状态变化历史",
            "mysql_engine": "InnoDB",
            "mysql_charset": "utf8mb4",
        },
    )
