"""Coordination records for reliable alarm time-series archiving."""

import sqlalchemy as sa
from sqlalchemy.orm import Mapped, mapped_column

from . import Base


def _key(length: int) -> sa.String:
    return sa.String(length).with_variant(sa.String(length, collation="utf8mb4_bin"), "mysql")


class AlarmControl(Base):
    __tablename__ = "alarm_control"

    alarm_id: Mapped[str] = mapped_column(_key(64))
    station_id: Mapped[str] = mapped_column(_key(64))
    device_id: Mapped[str] = mapped_column(_key(64))
    active_key: Mapped[str | None] = mapped_column(_key(128))
    occurred_at_us: Mapped[int] = mapped_column(sa.BigInteger)
    cleared_at_us: Mapped[int | None] = mapped_column(sa.BigInteger)
    acknowledged_at_us: Mapped[int | None] = mapped_column(sa.BigInteger)
    state_json: Mapped[str] = mapped_column(sa.Text)

    __table_args__ = (
        sa.UniqueConstraint("alarm_id", name="uq_alarm_control_id"),
        sa.UniqueConstraint("station_id", "active_key", name="uq_alarm_control_active_key"),
        sa.CheckConstraint(
            "(active_key IS NULL AND cleared_at_us IS NOT NULL) OR "
            "(active_key IS NOT NULL AND cleared_at_us IS NULL)",
            name="ck_alarm_control_active",
        ),
        sa.CheckConstraint(
            "occurred_at_us >= 0 AND "
            "(cleared_at_us IS NULL OR cleared_at_us >= occurred_at_us) AND "
            "(acknowledged_at_us IS NULL OR acknowledged_at_us >= occurred_at_us)",
            name="ck_alarm_control_times",
        ),
        sa.Index("ix_alarm_control_active", "station_id", "cleared_at_us", "alarm_id"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class AlarmOutbox(Base):
    __tablename__ = "alarm_outbox"

    event_id: Mapped[str] = mapped_column(_key(64))
    alarm_id: Mapped[str] = mapped_column(_key(64))
    station_id: Mapped[str] = mapped_column(_key(64))
    device_id: Mapped[str] = mapped_column(_key(64))
    stream_seq: Mapped[int] = mapped_column(sa.BigInteger)
    kind: Mapped[int] = mapped_column(sa.Integer)
    event_time_us: Mapped[int] = mapped_column(sa.BigInteger)
    event_json: Mapped[str] = mapped_column(sa.Text)
    state_json: Mapped[str] = mapped_column(sa.Text)

    __table_args__ = (
        sa.UniqueConstraint("event_id", name="uq_alarm_outbox_event"),
        sa.UniqueConstraint("station_id", "stream_seq", name="uq_alarm_outbox_sequence"),
        sa.UniqueConstraint("alarm_id", "kind", name="uq_alarm_outbox_lifecycle"),
        sa.CheckConstraint(
            "stream_seq > 0 AND event_time_us >= 0 AND kind IN (0, 1, 2)",
            name="ck_alarm_outbox_event",
        ),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class AlarmArchiveProgress(Base):
    __tablename__ = "alarm_archive_progress"

    station_id: Mapped[str] = mapped_column(_key(64))
    committed_seq: Mapped[int] = mapped_column(sa.BigInteger)
    archived_seq: Mapped[int] = mapped_column(sa.BigInteger)

    __table_args__ = (
        sa.UniqueConstraint("station_id", name="uq_alarm_archive_progress_station"),
        sa.CheckConstraint(
            "archived_seq >= 0 AND committed_seq >= archived_seq",
            name="ck_alarm_archive_progress_bounds",
        ),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )
