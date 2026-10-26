"""Shared mapped columns for the four independent power-point tables."""

from datetime import datetime
from typing import Any
import sqlalchemy as sa
from sqlalchemy.orm import Mapped, mapped_column
from ..types import integer, timestamp, TimestampDefault
from . import Base


class PointBase(Base):
    """Abstract common fields; each point category still has its own physical table."""

    __abstract__ = True
    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
        sort_order=-100,
    )
    point_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="稳定点 ID，四表之间也全局唯一（服务校验）"
    )
    owner_device_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="点归属节点；部件点填 rack/pack/cell"
    )
    binding_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, comment="实际采集来源绑定；部件点填 BMS 的绑定"
    )
    code: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    semantic_code: Mapped[str] = mapped_column(
        sa.String(64),
        nullable=False,
        server_default=sa.text("''"),
        comment="非空=固有点映射；空=厂家扩展点",
    )
    vendor_key: Mapped[str] = mapped_column(
        sa.String(120),
        nullable=False,
        server_default=sa.text("''"),
        comment="厂家点稳定键，重复导入据此更新",
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
    protocol_address_json: Mapped[Any | None] = mapped_column(
        sa.JSON(), nullable=True, comment="IEC104 casdu/ioa、DLT645 data_id、IEC61850 object_ref"
    )
    enabled: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    disabled_reason: Mapped[str] = mapped_column(
        sa.String(512), nullable=False, server_default=sa.text("''")
    )
    stale_after_ms: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
        comment="0 表示继承站默认",
    )
    source_import_row_id: Mapped[str] = mapped_column(
        sa.String(64), nullable=False, server_default=sa.text("''")
    )
    legacy_ref: Mapped[str | None] = mapped_column(sa.String(64), nullable=True)
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
