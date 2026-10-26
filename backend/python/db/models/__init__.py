"""Portable declarative ORM schema shared by sessions, initialization and Alembic."""

# ruff: noqa: E402

from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column
from ..types import integer


class Base(DeclarativeBase):
    """Base for all EMS persisted models."""

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True),
        primary_key=True,
        autoincrement=True,
        nullable=False,
        sort_order=-100,
    )


metadata = Base.metadata

from .point import PointBase

from .device_management import (
    Device,
    DeviceType,
    TypePoint,
    DeviceTemplate,
    Channel,
    Binding,
    YcPoint,
    YxPoint,
    YkPoint,
    YtPoint,
    ImportJob,
    ImportRow,
    CommStat5m,
    StateLog,
)  # noqa: E402, F401
from .history import (
    HistoryPointVersion,
    HistoryPointSeries,
    HistoryPointSnapshot,
    HistoryEventIdentity,
    HistoryQuarantine,
)  # noqa: E402, F401

__all__ = [
    "Base",
    "metadata",
    "PointBase",
    "Device",
    "DeviceType",
    "TypePoint",
    "DeviceTemplate",
    "Channel",
    "Binding",
    "YcPoint",
    "YxPoint",
    "YkPoint",
    "YtPoint",
    "ImportJob",
    "ImportRow",
    "CommStat5m",
    "StateLog",
    "HistoryPointVersion",
    "HistoryPointSeries",
    "HistoryPointSnapshot",
    "HistoryEventIdentity",
    "HistoryQuarantine",
]
