"""Declarative ORM models for history."""

from datetime import datetime
from typing import Any
import sqlalchemy as sa
from sqlalchemy.dialects import mysql
from sqlalchemy.orm import Mapped, mapped_column
from ..types import integer, timestamp, TimestampDefault
from . import Base


class HistoryPointVersion(Base):
    """history_point_version persisted rows."""

    __tablename__ = "history_point_version"
    scope_kind: Mapped[str] = mapped_column(sa.String(8), nullable=False)
    scope_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    config_version: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False), nullable=False, autoincrement=False
    )
    checksum: Mapped[str] = mapped_column(sa.CHAR(64), nullable=False)
    point_count: Mapped[int] = mapped_column(
        integer(size="integer", unsigned=True, boolean=False), nullable=False, autoincrement=False
    )
    ready_at: Mapped[datetime | None] = mapped_column(timestamp(microseconds=True), nullable=True)
    __table_args__ = (
        sa.UniqueConstraint(
            "scope_kind", "scope_id", "config_version", name="uq_history_version_key"
        ),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class HistoryPointSeries(Base):
    """history_point_series persisted rows."""

    __tablename__ = "history_point_series"
    point_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    value_type: Mapped[str] = mapped_column(sa.String(16), nullable=False)
    series_table: Mapped[str] = mapped_column(sa.String(96), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    retired_at: Mapped[datetime | None] = mapped_column(timestamp(microseconds=True), nullable=True)
    __table_args__ = (
        sa.UniqueConstraint("point_id", name="uq_history_series_point"),
        sa.CheckConstraint(
            "value_type IN ('numeric', 'integer', 'boolean', 'text')", name="ck_history_series_type"
        ),
        sa.Index("uk_history_series_table", "series_table", unique=True),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class HistoryPointSnapshot(Base):
    """history_point_snapshot persisted rows."""

    __tablename__ = "history_point_snapshot"
    scope_kind: Mapped[str] = mapped_column(sa.String(8), nullable=False)
    scope_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    config_version: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True, boolean=False), nullable=False, autoincrement=False
    )
    point_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    owner_device_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    point_code: Mapped[str] = mapped_column(sa.String(128), nullable=False)
    display_name: Mapped[str] = mapped_column(sa.String(256), nullable=False)
    point_kind: Mapped[str] = mapped_column(sa.String(16), nullable=False)
    value_type: Mapped[str] = mapped_column(sa.String(16), nullable=False)
    unit: Mapped[str | None] = mapped_column(sa.String(32), nullable=True)
    definition_id: Mapped[str | None] = mapped_column(sa.String(64), nullable=True)
    definition_version: Mapped[int | None] = mapped_column(
        integer(size="big", unsigned=True, boolean=False), nullable=True, autoincrement=False
    )
    enabled: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=False, boolean=True), nullable=False
    )
    device_path: Mapped[Any] = mapped_column(sa.JSON(), nullable=False)
    __table_args__ = (
        sa.UniqueConstraint(
            "scope_kind", "scope_id", "config_version", "point_id", name="uq_history_snapshot_key"
        ),
        sa.ForeignKeyConstraint(
            ["scope_kind", "scope_id", "config_version"],
            [
                "history_point_version.scope_kind",
                "history_point_version.scope_id",
                "history_point_version.config_version",
            ],
            name="fk_history_point_version",
        ),
        sa.ForeignKeyConstraint(
            ["point_id"], ["history_point_series.point_id"], name="fk_history_point_series"
        ),
        sa.Index("ix_history_point", "point_id", "scope_kind", "scope_id", "config_version"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class HistoryEventIdentity(Base):
    """history_event_identity persisted rows."""

    __tablename__ = "history_event_identity"
    event_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    point_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    source_time_us: Mapped[int] = mapped_column(
        integer(size="big", unsigned=False, boolean=False), nullable=False, autoincrement=False
    )
    payload_sha256: Mapped[str] = mapped_column(sa.CHAR(64), nullable=False)
    first_seen_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    __table_args__ = (
        sa.UniqueConstraint("event_id", name="uq_history_event_id"),
        sa.Index("ix_history_identity_point_time", "point_id", "source_time_us"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class HistoryQuarantine(Base):
    """history_quarantine persisted rows."""

    __tablename__ = "history_quarantine"
    stream_scope: Mapped[str] = mapped_column(sa.String(128), nullable=False)
    stream_id: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    event_id: Mapped[str | None] = mapped_column(sa.String(64), nullable=True)
    reason: Mapped[str] = mapped_column(sa.String(256), nullable=False)
    raw_payload: Mapped[bytes] = mapped_column(
        sa.LargeBinary().with_variant(mysql.MEDIUMBLOB(), "mysql"), nullable=False
    )
    status: Mapped[str] = mapped_column(
        sa.String(16), nullable=False, server_default=sa.text("'open'")
    )
    created_at: Mapped[datetime] = mapped_column(
        timestamp(microseconds=True),
        nullable=False,
        server_default=TimestampDefault(on_update=False, microseconds=True),
    )
    resolved_at: Mapped[datetime | None] = mapped_column(
        timestamp(microseconds=True), nullable=True
    )
    __table_args__ = (
        sa.UniqueConstraint("stream_scope", "stream_id", name="uq_history_quarantine_key"),
        sa.Index("ix_history_quarantine_status", "status", "created_at"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )
