"""Portable declarative ORM schema shared by sessions, initialization and Alembic."""

# ruff: noqa: E402

from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """Base for all EMS persisted models."""


metadata = Base.metadata

from .point import PointBase

from .configuration import (
    LegacyDevice,
    LegacyChannel,
    LegacyYcPoint,
    LegacyYxPoint,
    LegacyYkPoint,
    LegacyYtPoint,
)  # noqa: E402, F401
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
    "LegacyDevice",
    "LegacyChannel",
    "LegacyYcPoint",
    "LegacyYxPoint",
    "LegacyYkPoint",
    "LegacyYtPoint",
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
