"""Initial independent EMS Web schema (5 tables).

Frozen DDL; this revision does not import mutable application metadata.
"""

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import mysql, postgresql

revision = "82a1fe470dc9"
down_revision = None
branch_labels = None
depends_on = None


def upgrade() -> None:

    op.create_table(
        "audit_log",
        sa.Column(
            "id",
            sa.BigInteger()
            .with_variant(mysql.BIGINT(unsigned=True), "mysql")
            .with_variant(sa.Integer(), "sqlite"),
            autoincrement=True,
            nullable=False,
        ),
        sa.Column(
            "event_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "actor_user_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=True,
        ),
        sa.Column(
            "target_user_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=True,
        ),
        sa.Column("action", sa.String(length=64), nullable=False),
        sa.Column("resource_type", sa.String(length=32), nullable=False),
        sa.Column(
            "resource_id",
            sa.String(length=128).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=128), "mysql"
            ),
            nullable=True,
        ),
        sa.Column(
            "outcome",
            sa.Enum(
                "success",
                "failure",
                name="ck_audit_log_outcome",
                native_enum=False,
                create_constraint=True,
            ).with_variant(mysql.ENUM("success", "failure"), "mysql"),
            nullable=False,
        ),
        sa.Column("reason", sa.String(length=64), nullable=True),
        sa.Column(
            "request_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column("source_ip", sa.String(length=45), nullable=True),
        sa.Column("details", sa.JSON(none_as_null=True), nullable=True),
        sa.Column(
            "occurred_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("event_id", name="uq_audit_log_event_id"),
        mysql_charset="utf8mb4",
        mysql_engine="InnoDB",
    )
    op.create_index(
        "ix_audit_log_actor", "audit_log", ["actor_user_id", "occurred_at"], unique=False
    )
    op.create_index("ix_audit_log_occurred", "audit_log", ["occurred_at", "id"], unique=False)
    op.create_index(
        "ix_audit_log_target", "audit_log", ["target_user_id", "occurred_at"], unique=False
    )
    op.create_table(
        "auth_login_policy",
        sa.Column(
            "id",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            autoincrement=False,
            nullable=False,
        ),
        sa.Column(
            "failed_login_limit",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            server_default=sa.text("6"),
            nullable=False,
        ),
        sa.Column(
            "lock_minutes",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            server_default=sa.text("10"),
            nullable=False,
        ),
        sa.Column(
            "idle_timeout_minutes",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            server_default=sa.text("10"),
            nullable=False,
        ),
        sa.Column(
            "password_min_length",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            server_default=sa.text("12"),
            nullable=False,
        ),
        sa.Column(
            "login_notice", sa.String(length=500), server_default=sa.text("''"), nullable=False
        ),
        sa.Column(
            "updated_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.CheckConstraint("failed_login_limit BETWEEN 3 AND 6", name="ck_auth_policy_failures"),
        sa.CheckConstraint("id = 1", name="ck_auth_login_policy_singleton"),
        sa.CheckConstraint("idle_timeout_minutes BETWEEN 10 AND 30", name="ck_auth_policy_idle"),
        sa.CheckConstraint("length(login_notice) <= 500", name="ck_auth_policy_notice"),
        sa.CheckConstraint("lock_minutes BETWEEN 10 AND 30", name="ck_auth_policy_lock"),
        sa.CheckConstraint("password_min_length BETWEEN 12 AND 32", name="ck_auth_policy_password"),
        sa.PrimaryKeyConstraint("id"),
        mysql_charset="utf8mb4",
        mysql_engine="InnoDB",
    )
    op.create_table(
        "auth_user",
        sa.Column(
            "id",
            sa.BigInteger()
            .with_variant(mysql.BIGINT(unsigned=True), "mysql")
            .with_variant(sa.Integer(), "sqlite"),
            autoincrement=True,
            nullable=False,
        ),
        sa.Column(
            "user_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "username",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(charset="ascii", collation="ascii_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column("display_name", sa.String(length=64), nullable=False),
        sa.Column("password_hash", sa.String(length=255), nullable=False),
        sa.Column(
            "role_code",
            sa.Enum(
                "admin",
                "operator",
                "observer",
                name="ck_auth_user_role",
                native_enum=False,
                create_constraint=True,
            ).with_variant(mysql.ENUM("admin", "operator", "observer"), "mysql"),
            server_default=sa.text("'observer'"),
            nullable=False,
        ),
        sa.Column(
            "status",
            sa.Enum(
                "enabled",
                "disabled",
                "deleted",
                name="ck_auth_user_status",
                native_enum=False,
                create_constraint=True,
            ).with_variant(mysql.ENUM("enabled", "disabled", "deleted"), "mysql"),
            server_default=sa.text("'disabled'"),
            nullable=False,
        ),
        sa.Column(
            "scope_mode",
            sa.Enum(
                "all",
                "selected",
                name="ck_auth_user_scope_mode",
                native_enum=False,
                create_constraint=True,
            ).with_variant(mysql.ENUM("all", "selected"), "mysql"),
            server_default=sa.text("'selected'"),
            nullable=False,
        ),
        sa.Column(
            "must_change_password",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            server_default=sa.text("1"),
            nullable=False,
        ),
        sa.Column(
            "temporary_password_expires_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=True,
        ),
        sa.Column(
            "failed_attempts",
            sa.Integer().with_variant(mysql.TINYINT(unsigned=True), "mysql"),
            server_default=sa.text("0"),
            nullable=False,
        ),
        sa.Column(
            "locked_until",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=True,
        ),
        sa.Column(
            "password_changed_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=True,
        ),
        sa.Column(
            "created_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.Column(
            "updated_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.Column(
            "last_login_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=True,
        ),
        sa.Column(
            "deleted_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=True,
        ),
        sa.CheckConstraint(
            "(status = 'deleted' AND deleted_at IS NOT NULL) OR (status <> 'deleted' AND deleted_at IS NULL)",
            name="ck_auth_user_deleted",
        ),
        sa.CheckConstraint("scope_mode <> 'all' OR role_code = 'admin'", name="ck_auth_user_all"),
        sa.CheckConstraint(
            "(must_change_password = 1 AND temporary_password_expires_at IS NOT NULL) OR (must_change_password = 0 AND temporary_password_expires_at IS NULL)",
            name="ck_auth_user_temporary_password",
        ),
        sa.CheckConstraint("failed_attempts BETWEEN 0 AND 6", name="ck_auth_user_failures"),
        sa.CheckConstraint("length(trim(display_name)) BETWEEN 1 AND 64", name="ck_auth_user_name"),
        sa.CheckConstraint(
            "length(username) BETWEEN 4 AND 64 AND username = lower(username)",
            name="ck_auth_user_username",
        ),
        sa.CheckConstraint("must_change_password IN (0, 1)", name="ck_auth_user_change_flag"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("user_id", name="uq_auth_user_user_id"),
        sa.UniqueConstraint("username", name="uq_auth_user_username"),
        mysql_charset="utf8mb4",
        mysql_engine="InnoDB",
    )
    op.create_index("ix_auth_user_created", "auth_user", ["created_at", "user_id"], unique=False)
    op.create_index("ix_auth_user_status_role", "auth_user", ["status", "role_code"], unique=False)
    op.create_table(
        "auth_session",
        sa.Column(
            "id",
            sa.BigInteger()
            .with_variant(mysql.BIGINT(unsigned=True), "mysql")
            .with_variant(sa.Integer(), "sqlite"),
            autoincrement=True,
            nullable=False,
        ),
        sa.Column(
            "session_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "user_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "kind",
            sa.Enum(
                "full",
                "password_change",
                name="ck_auth_session_kind",
                native_enum=False,
                create_constraint=True,
            ).with_variant(mysql.ENUM("full", "password_change"), "mysql"),
            nullable=False,
        ),
        sa.Column(
            "token_hash",
            sa.LargeBinary(length=32).with_variant(sa.BINARY(length=32), "mysql"),
            nullable=False,
        ),
        sa.Column(
            "csrf_token",
            sa.CHAR(length=43).with_variant(
                mysql.CHAR(charset="ascii", collation="ascii_bin", length=43), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "created_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.Column(
            "auth_time",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.Column(
            "last_interactive_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.Column(
            "absolute_expires_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=False,
        ),
        sa.Column(
            "revoked_at",
            sa.DateTime()
            .with_variant(mysql.DATETIME(fsp=3), "mysql")
            .with_variant(postgresql.TIMESTAMP(precision=3), "postgresql"),
            nullable=True,
        ),
        sa.Column("revoke_reason", sa.String(length=32), nullable=True),
        sa.Column("source_ip", sa.String(length=45), nullable=True),
        sa.Column(
            "user_agent", sa.String(length=256), server_default=sa.text("''"), nullable=False
        ),
        sa.CheckConstraint(
            "(revoked_at IS NULL AND revoke_reason IS NULL) OR (revoked_at IS NOT NULL AND revoke_reason IS NOT NULL)",
            name="ck_auth_session_revocation",
        ),
        sa.CheckConstraint("absolute_expires_at > created_at", name="ck_auth_session_deadline"),
        sa.CheckConstraint("length(csrf_token) = 43", name="ck_auth_session_csrf"),
        sa.CheckConstraint("length(token_hash) = 32", name="ck_auth_session_token_hash"),
        sa.ForeignKeyConstraint(["user_id"], ["auth_user.user_id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("session_id", name="uq_auth_session_session_id"),
        sa.UniqueConstraint("token_hash", name="uq_auth_session_token_hash"),
        mysql_charset="utf8mb4",
        mysql_engine="InnoDB",
    )
    op.create_index(
        "ix_auth_session_expires", "auth_session", ["absolute_expires_at"], unique=False
    )
    op.create_index(
        "ix_auth_session_user_active",
        "auth_session",
        ["user_id", "revoked_at", "last_interactive_at"],
        unique=False,
    )
    op.create_table(
        "auth_user_scope",
        sa.Column(
            "id",
            sa.BigInteger()
            .with_variant(mysql.BIGINT(unsigned=True), "mysql")
            .with_variant(sa.Integer(), "sqlite"),
            autoincrement=True,
            nullable=False,
        ),
        sa.Column(
            "user_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "station_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            nullable=False,
        ),
        sa.Column(
            "device_root_id",
            sa.String(length=64).with_variant(
                mysql.VARCHAR(collation="utf8mb4_bin", length=64), "mysql"
            ),
            server_default=sa.text("''"),
            nullable=False,
        ),
        sa.CheckConstraint("length(station_id) > 0", name="ck_auth_scope_station"),
        sa.ForeignKeyConstraint(["user_id"], ["auth_user.user_id"], ondelete="RESTRICT"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("user_id", "station_id", "device_root_id", name="uq_auth_user_scope"),
        mysql_charset="utf8mb4",
        mysql_engine="InnoDB",
    )


def downgrade() -> None:

    op.drop_table("auth_user_scope")
    op.drop_index("ix_auth_session_user_active", table_name="auth_session")
    op.drop_index("ix_auth_session_expires", table_name="auth_session")
    op.drop_table("auth_session")
    op.drop_index("ix_auth_user_status_role", table_name="auth_user")
    op.drop_index("ix_auth_user_created", table_name="auth_user")
    op.drop_table("auth_user")
    op.drop_table("auth_login_policy")
    op.drop_index("ix_audit_log_target", table_name="audit_log")
    op.drop_index("ix_audit_log_occurred", table_name="audit_log")
    op.drop_index("ix_audit_log_actor", table_name="audit_log")
    op.drop_table("audit_log")
