"""Independent declarative registry for the five EMS Web business tables."""

# ruff: noqa: E402

from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column

from db.types import integer


class WebBase(DeclarativeBase):
    """Web tables never register with the engineering Base."""

    id: Mapped[int] = mapped_column(
        integer(size="big", unsigned=True),
        primary_key=True,
        autoincrement=True,
        nullable=False,
        sort_order=-100,
    )


metadata = WebBase.metadata

from .identity import AuthUser, AuthUserScope, AuthSession, AuthLoginPolicy, AuditLog

__all__ = [
    "WebBase",
    "metadata",
    "AuthUser",
    "AuthUserScope",
    "AuthSession",
    "AuthLoginPolicy",
    "AuditLog",
]
