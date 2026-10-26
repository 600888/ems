"""Declarative ORM models for configuration."""

from datetime import datetime
import sqlalchemy as sa
from sqlalchemy.orm import Mapped, mapped_column
from ..types import integer, timestamp, TimestampDefault
from . import Base


class LegacyDevice(Base):
    """device persisted rows."""

    __tablename__ = "device"

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    code: Mapped[str] = mapped_column(sa.String(64), nullable=False, server_default=sa.text("''"))
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    device_type: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    group_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    description: Mapped[str | None] = mapped_column(
        sa.String(256), nullable=True, server_default=sa.text("''")
    )
    enable: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    created_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=False, microseconds=False),
    )
    updated_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=True, microseconds=False),
        onupdate=sa.func.current_timestamp(),
    )

    __table_args__ = (
        sa.Index("uq_configuration_device_code", "code", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class LegacyChannel(Base):
    """channel persisted rows."""

    __tablename__ = "channel"

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    code: Mapped[str] = mapped_column(sa.String(64), nullable=False, server_default=sa.text("''"))
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    device_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    protocol_type: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    conn_type: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("2"),
    )
    ip: Mapped[str | None] = mapped_column(
        sa.String(64), nullable=True, server_default=sa.text("'127.0.0.1'")
    )
    port: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("502"),
    )
    com_port: Mapped[str | None] = mapped_column(
        sa.String(64), nullable=True, server_default=sa.text("''")
    )
    baud_rate: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("9600"),
    )
    data_bits: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("8"),
    )
    stop_bits: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    parity: Mapped[str | None] = mapped_column(
        sa.String(8), nullable=True, server_default=sa.text("'N'")
    )
    rtu_addr: Mapped[str | None] = mapped_column(
        sa.String(16), nullable=True, server_default=sa.text("'1'")
    )
    timeout: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("5"),
    )
    enable: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    created_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=False, microseconds=False),
    )
    updated_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=True, microseconds=False),
        onupdate=sa.func.current_timestamp(),
    )

    __table_args__ = (
        sa.Index("uq_configuration_channel_code", "code", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class LegacyYcPoint(Base):
    """point_yc persisted rows."""

    __tablename__ = "point_yc"

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    code: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    channel_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    device_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    protocol_type: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    func_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("3"),
    )
    slave_id: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    reg_addr: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    bit_position: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    data_id: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    casdu: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    ioa: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    object_ref: Mapped[str | None] = mapped_column(
        sa.String(128), nullable=True, server_default=sa.text("''")
    )
    decode_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    endian: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    iec104_type: Mapped[str | None] = mapped_column(
        sa.String(32), nullable=True, server_default=sa.text("''")
    )
    mul_coe: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("1.0")
    )
    add_coe: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("0.0")
    )
    min_limit: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("-9999999.0")
    )
    max_limit: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("9999999.0")
    )
    unit: Mapped[str | None] = mapped_column(
        sa.String(32), nullable=True, server_default=sa.text("''")
    )
    enable: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    description: Mapped[str | None] = mapped_column(
        sa.String(256), nullable=True, server_default=sa.text("''")
    )
    created_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=False, microseconds=False),
    )
    updated_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=True, microseconds=False),
        onupdate=sa.func.current_timestamp(),
    )

    __table_args__ = (
        sa.Index("uq_point_yc_code", "code", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class LegacyYxPoint(Base):
    """point_yx persisted rows."""

    __tablename__ = "point_yx"

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    code: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    channel_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    device_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    protocol_type: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    func_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    slave_id: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    reg_addr: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    bit_position: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    data_id: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    casdu: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    ioa: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    object_ref: Mapped[str | None] = mapped_column(
        sa.String(128), nullable=True, server_default=sa.text("''")
    )
    decode_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    endian: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    iec104_type: Mapped[str | None] = mapped_column(
        sa.String(32), nullable=True, server_default=sa.text("''")
    )
    bit: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    reverse_flag: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    enable: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    description: Mapped[str | None] = mapped_column(
        sa.String(256), nullable=True, server_default=sa.text("''")
    )
    created_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=False, microseconds=False),
    )
    updated_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=True, microseconds=False),
        onupdate=sa.func.current_timestamp(),
    )

    __table_args__ = (
        sa.Index("uq_point_yx_code", "code", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class LegacyYkPoint(Base):
    """point_yk persisted rows."""

    __tablename__ = "point_yk"

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    code: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    channel_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    device_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    protocol_type: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    func_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("5"),
    )
    slave_id: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    reg_addr: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    bit_position: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    data_id: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    casdu: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    ioa: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    object_ref: Mapped[str | None] = mapped_column(
        sa.String(128), nullable=True, server_default=sa.text("''")
    )
    decode_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    endian: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    iec104_type: Mapped[str | None] = mapped_column(
        sa.String(32), nullable=True, server_default=sa.text("''")
    )
    bit: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    command_type: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    related_yx_id: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    enable: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    description: Mapped[str | None] = mapped_column(
        sa.String(256), nullable=True, server_default=sa.text("''")
    )
    created_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=False, microseconds=False),
    )
    updated_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=True, microseconds=False),
        onupdate=sa.func.current_timestamp(),
    )

    __table_args__ = (
        sa.Index("uq_point_yk_code", "code", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class LegacyYtPoint(Base):
    """point_yt persisted rows."""

    __tablename__ = "point_yt"

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=True,
        primary_key=True,
    )
    code: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    name: Mapped[str] = mapped_column(sa.String(128), nullable=False, server_default=sa.text("''"))
    channel_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    device_id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=False,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    protocol_type: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    func_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("6"),
    )
    slave_id: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    reg_addr: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    bit_position: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    data_id: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    casdu: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    ioa: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    object_ref: Mapped[str | None] = mapped_column(
        sa.String(128), nullable=True, server_default=sa.text("''")
    )
    decode_code: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    endian: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    iec104_type: Mapped[str | None] = mapped_column(
        sa.String(32), nullable=True, server_default=sa.text("''")
    )
    related_yc_id: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("0"),
    )
    mul_coe: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("1.0")
    )
    add_coe: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("0.0")
    )
    min_limit: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("-9999999.0")
    )
    max_limit: Mapped[float | None] = mapped_column(
        sa.Double(), nullable=True, server_default=sa.text("9999999.0")
    )
    unit: Mapped[str | None] = mapped_column(
        sa.String(32), nullable=True, server_default=sa.text("''")
    )
    enable: Mapped[int | None] = mapped_column(
        integer(size="integer", unsigned=False, boolean=False),
        nullable=True,
        autoincrement=False,
        server_default=sa.text("1"),
    )
    description: Mapped[str | None] = mapped_column(
        sa.String(256), nullable=True, server_default=sa.text("''")
    )
    created_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=False, microseconds=False),
    )
    updated_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=False),
        nullable=True,
        server_default=TimestampDefault(on_update=True, microseconds=False),
        onupdate=sa.func.current_timestamp(),
    )

    __table_args__ = (
        sa.Index("uq_point_yt_code", "code", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )
