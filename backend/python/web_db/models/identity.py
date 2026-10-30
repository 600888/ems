"""The five ems_web tables, using a registry separate from db.models.Base."""

from datetime import datetime

import sqlalchemy as sa
from sqlalchemy.dialects import mysql, postgresql
from sqlalchemy.orm import Mapped, mapped_column

from db.types import enumeration, integer
from . import WebBase


def identifier(length: int = 64):
    """Keep stable identifiers case sensitive even on a case-insensitive MySQL database."""
    return sa.String(length).with_variant(mysql.VARCHAR(length, collation="utf8mb4_bin"), "mysql")


def ascii_text(length: int, *, fixed: bool = False):
    generic = sa.CHAR(length) if fixed else sa.String(length)
    specific = mysql.CHAR if fixed else mysql.VARCHAR
    return generic.with_variant(specific(length, charset="ascii", collation="ascii_bin"), "mysql")


def timestamp():
    """Business services supply UTC values; these columns never update themselves."""
    return (
        sa.DateTime()
        .with_variant(mysql.DATETIME(fsp=3), "mysql")
        .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql")
    )


class AuthUser(WebBase):
    """Account and authentication state; roles remain fixed values rather than extra tables."""

    __tablename__ = "auth_user"

    user_id: Mapped[str] = mapped_column(identifier(), nullable=False)
    username: Mapped[str] = mapped_column(ascii_text(64), nullable=False)
    display_name: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    password_hash: Mapped[str] = mapped_column(sa.String(255), nullable=False)
    role_code: Mapped[str] = mapped_column(
        enumeration("admin", "operator", "observer", name="ck_auth_user_role"),
        nullable=False,
        server_default=sa.text("'observer'"),
    )
    status: Mapped[str] = mapped_column(
        enumeration("enabled", "disabled", "deleted", name="ck_auth_user_status"),
        nullable=False,
        server_default=sa.text("'disabled'"),
    )
    scope_mode: Mapped[str] = mapped_column(
        enumeration("all", "selected", name="ck_auth_user_scope_mode"),
        nullable=False,
        server_default=sa.text("'selected'"),
    )
    must_change_password: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        nullable=False,
        server_default=sa.text("1"),
    )
    temporary_password_expires_at: Mapped[datetime | None] = mapped_column(timestamp())
    failed_attempts: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        nullable=False,
        server_default=sa.text("0"),
    )
    locked_until: Mapped[datetime | None] = mapped_column(timestamp())
    password_changed_at: Mapped[datetime | None] = mapped_column(timestamp())
    created_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)
    updated_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)
    last_login_at: Mapped[datetime | None] = mapped_column(timestamp())
    deleted_at: Mapped[datetime | None] = mapped_column(timestamp())

    __table_args__ = (
        sa.UniqueConstraint("user_id", name="uq_auth_user_user_id"),
        sa.UniqueConstraint("username", name="uq_auth_user_username"),
        sa.CheckConstraint(
            "length(username) BETWEEN 4 AND 64 AND username = lower(username)",
            name="ck_auth_user_username",
        ),
        sa.CheckConstraint("length(trim(display_name)) BETWEEN 1 AND 64", name="ck_auth_user_name"),
        sa.CheckConstraint("scope_mode <> 'all' OR role_code = 'admin'", name="ck_auth_user_all"),
        sa.CheckConstraint("must_change_password IN (0, 1)", name="ck_auth_user_change_flag"),
        sa.CheckConstraint(
            "(must_change_password = 1 AND temporary_password_expires_at IS NOT NULL) OR "
            "(must_change_password = 0 AND temporary_password_expires_at IS NULL)",
            name="ck_auth_user_temporary_password",
        ),
        sa.CheckConstraint("failed_attempts BETWEEN 0 AND 6", name="ck_auth_user_failures"),
        sa.CheckConstraint(
            "(status = 'deleted' AND deleted_at IS NOT NULL) OR "
            "(status <> 'deleted' AND deleted_at IS NULL)",
            name="ck_auth_user_deleted",
        ),
        sa.Index("ix_auth_user_status_role", "status", "role_code"),
        sa.Index("ix_auth_user_created", "created_at", "user_id"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class AuthUserScope(WebBase):
    """A station or subtree grant. Empty device_root_id represents an entire station."""

    __tablename__ = "auth_user_scope"

    user_id: Mapped[str] = mapped_column(
        identifier(),
        sa.ForeignKey("auth_user.user_id", ondelete="RESTRICT"),
        nullable=False,
    )
    station_id: Mapped[str] = mapped_column(identifier(), nullable=False)
    device_root_id: Mapped[str] = mapped_column(
        identifier(),
        nullable=False,
        server_default=sa.text("''"),
    )

    __table_args__ = (
        sa.UniqueConstraint("user_id", "station_id", "device_root_id", name="uq_auth_user_scope"),
        sa.CheckConstraint("length(station_id) > 0", name="ck_auth_scope_station"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class AuthSession(WebBase):
    """Persistent opaque-Cookie session; permissions are never copied into the row."""

    __tablename__ = "auth_session"

    session_id: Mapped[str] = mapped_column(identifier(), nullable=False)
    user_id: Mapped[str] = mapped_column(
        identifier(),
        sa.ForeignKey("auth_user.user_id", ondelete="RESTRICT"),
        nullable=False,
    )
    kind: Mapped[str] = mapped_column(
        enumeration("full", "password_change", name="ck_auth_session_kind"),
        nullable=False,
    )
    token_hash: Mapped[bytes] = mapped_column(
        sa.LargeBinary(32).with_variant(mysql.BINARY(32), "mysql"),
        nullable=False,
    )
    csrf_token: Mapped[str] = mapped_column(ascii_text(43, fixed=True), nullable=False)
    created_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)
    auth_time: Mapped[datetime] = mapped_column(timestamp(), nullable=False)
    last_interactive_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)
    absolute_expires_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)
    revoked_at: Mapped[datetime | None] = mapped_column(timestamp())
    revoke_reason: Mapped[str | None] = mapped_column(sa.String(32))
    source_ip: Mapped[str | None] = mapped_column(sa.String(45))
    user_agent: Mapped[str] = mapped_column(
        sa.String(256),
        nullable=False,
        server_default=sa.text("''"),
    )

    __table_args__ = (
        sa.UniqueConstraint("session_id", name="uq_auth_session_session_id"),
        sa.UniqueConstraint("token_hash", name="uq_auth_session_token_hash"),
        sa.CheckConstraint("length(token_hash) = 32", name="ck_auth_session_token_hash"),
        sa.CheckConstraint("length(csrf_token) = 43", name="ck_auth_session_csrf"),
        sa.CheckConstraint("absolute_expires_at > created_at", name="ck_auth_session_deadline"),
        sa.CheckConstraint(
            "(revoked_at IS NULL AND revoke_reason IS NULL) OR "
            "(revoked_at IS NOT NULL AND revoke_reason IS NOT NULL)",
            name="ck_auth_session_revocation",
        ),
        sa.Index("ix_auth_session_user_active", "user_id", "revoked_at", "last_interactive_at"),
        sa.Index("ix_auth_session_expires", "absolute_expires_at"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class AuthLoginPolicy(WebBase):
    """The sole policy row also serializes administrator management transactions."""

    __tablename__ = "auth_login_policy"

    id: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        primary_key=True,
        autoincrement=False,
        nullable=False,
        sort_order=-100,
    )
    failed_login_limit: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        nullable=False,
        server_default=sa.text("6"),
    )
    lock_minutes: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        nullable=False,
        server_default=sa.text("10"),
    )
    idle_timeout_minutes: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        nullable=False,
        server_default=sa.text("10"),
    )
    password_min_length: Mapped[int] = mapped_column(
        integer(size="tiny", unsigned=True),
        nullable=False,
        server_default=sa.text("12"),
    )
    login_notice: Mapped[str] = mapped_column(
        sa.String(500),
        nullable=False,
        server_default=sa.text("''"),
    )
    updated_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)

    __table_args__ = (
        sa.CheckConstraint("id = 1", name="ck_auth_login_policy_singleton"),
        sa.CheckConstraint("failed_login_limit BETWEEN 3 AND 6", name="ck_auth_policy_failures"),
        sa.CheckConstraint("lock_minutes BETWEEN 10 AND 30", name="ck_auth_policy_lock"),
        sa.CheckConstraint("idle_timeout_minutes BETWEEN 10 AND 30", name="ck_auth_policy_idle"),
        sa.CheckConstraint("password_min_length BETWEEN 12 AND 32", name="ck_auth_policy_password"),
        sa.CheckConstraint("length(login_notice) <= 500", name="ck_auth_policy_notice"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )


class AuditLog(WebBase):
    """Append-only Web audit data; no foreign keys cascade from users or engineering data."""

    __tablename__ = "audit_log"

    event_id: Mapped[str] = mapped_column(identifier(), nullable=False)
    actor_user_id: Mapped[str | None] = mapped_column(identifier())
    target_user_id: Mapped[str | None] = mapped_column(identifier())
    action: Mapped[str] = mapped_column(sa.String(64), nullable=False)
    resource_type: Mapped[str] = mapped_column(sa.String(32), nullable=False)
    resource_id: Mapped[str | None] = mapped_column(identifier(128))
    outcome: Mapped[str] = mapped_column(
        enumeration("success", "failure", name="ck_audit_log_outcome"),
        nullable=False,
    )
    reason: Mapped[str | None] = mapped_column(sa.String(64))
    request_id: Mapped[str] = mapped_column(identifier(), nullable=False)
    source_ip: Mapped[str | None] = mapped_column(sa.String(45))
    details: Mapped[dict | None] = mapped_column(sa.JSON(none_as_null=True))
    occurred_at: Mapped[datetime] = mapped_column(timestamp(), nullable=False)

    __table_args__ = (
        sa.UniqueConstraint("event_id", name="uq_audit_log_event_id"),
        sa.Index("ix_audit_log_occurred", "occurred_at", "id"),
        sa.Index("ix_audit_log_actor", "actor_user_id", "occurred_at"),
        sa.Index("ix_audit_log_target", "target_user_id", "occurred_at"),
        {"mysql_engine": "InnoDB", "mysql_charset": "utf8mb4"},
    )
