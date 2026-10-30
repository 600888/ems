import importlib
import io
from datetime import datetime, timedelta

import pytest
import sqlalchemy as sa
from alembic import command
from alembic.migration import MigrationContext
from alembic.operations import Operations

from db.cli import main, migration_config
from db.config import make_engine, session_factory
from db.models import Base
from db.schema import schema_differences, comparison_options
from db.targets import require_target
from db.tests.test_database import ddl_statements
from web_db.models import WebBase, AuthUser, AuthUserScope, AuthSession, AuthLoginPolicy, AuditLog
from web_db.seeds import apply_seed

BASELINE = importlib.import_module("web_db.migrations.versions.initial_schema")
TABLES = {"auth_user", "auth_user_scope", "auth_session", "auth_login_policy", "audit_log"}
NOW = datetime(2026, 10, 30, 0, 0)


@pytest.fixture
def engine():
    engine = make_engine("sqlite:///:memory:")
    with engine.begin() as connection:
        command.upgrade(migration_config(connection=connection, database="ems_web"), "head")
    yield engine
    engine.dispose()


def user(**changes):
    values = dict(
        user_id="u1",
        username="testuser",
        display_name="测试用户",
        password_hash="encoded-hash",
        temporary_password_expires_at=NOW + timedelta(days=1),
        created_at=NOW,
        updated_at=NOW,
    )
    return AuthUser(**(values | changes))


def session_row(**changes):
    values = dict(
        session_id="s1",
        user_id="u1",
        kind="full",
        token_hash=b"h" * 32,
        csrf_token="c" * 43,
        created_at=NOW,
        auth_time=NOW,
        last_interactive_at=NOW,
        absolute_expires_at=NOW + timedelta(hours=8),
    )
    return AuthSession(**(values | changes))


def test_independent_registries_and_foreign_keys():
    assert set(WebBase.metadata.tables) == TABLES
    assert len(Base.metadata.tables) == 19
    assert not set(Base.metadata.tables) & TABLES
    assert WebBase.registry is not Base.registry
    for table in WebBase.metadata.tables.values():
        assert list(table.c)[0].name == "id"
        assert [c.name for c in table.primary_key] == ["id"]
        assert table.c.id.autoincrement is (table.name != "auth_login_policy")
        assert all(fk.column.table.metadata is WebBase.metadata for fk in table.foreign_keys)
    assert not AuditLog.__table__.foreign_keys


@pytest.mark.parametrize("dialect", ["sqlite", "mysql", "postgresql"])
def test_frozen_migration_matches_web_orm_for_each_dialect(dialect):
    expected = []
    mock = sa.create_mock_engine(
        dialect + "://",
        lambda ddl, *a, **kw: expected.append(str(ddl.compile(dialect=mock.dialect))),
    )
    WebBase.metadata.create_all(mock)
    buffer = io.StringIO()
    context = MigrationContext.configure(
        dialect=mock.dialect, opts={"as_sql": True, "output_buffer": buffer}
    )
    with Operations.context(context):
        BASELINE.upgrade()
    assert ddl_statements(buffer.getvalue()) == ddl_statements(";\n".join(expected))
    if dialect == "mysql":
        assert "BINARY(32)" in buffer.getvalue()
        assert "ascii_bin" in buffer.getvalue()
        assert "DATETIME(3)" in buffer.getvalue()


def test_web_upgrade_downgrade_and_policy_seed(engine):
    factory = session_factory(engine)
    with engine.connect() as connection:
        assert set(sa.inspect(connection).get_table_names()) == TABLES | {"alembic_version"}
        assert schema_differences(connection, metadata=WebBase.metadata, retired_tables=()) == []
    with factory.begin() as session:
        apply_seed(session, "policy")
    with factory.begin() as session:
        policy = session.get(AuthLoginPolicy, 1)
        assert (
            policy.failed_login_limit,
            policy.lock_minutes,
            policy.idle_timeout_minutes,
            policy.password_min_length,
        ) == (6, 10, 10, 12)
        policy.login_notice = "现场通知"
    with factory.begin() as session:
        apply_seed(session, "policy")
    with factory() as session:
        assert session.get(AuthLoginPolicy, 1).login_notice == "现场通知"
        assert session.scalar(sa.select(sa.func.count()).select_from(AuthUser)) == 0
        assert session.scalar(sa.select(sa.func.count()).select_from(AuthSession)) == 0
    with engine.begin() as connection:
        command.downgrade(migration_config(connection=connection, database="ems_web"), "base")
        assert set(sa.inspect(connection).get_table_names()) == {"alembic_version"}
        command.upgrade(migration_config(connection=connection, database="ems_web"), "head")


@pytest.mark.parametrize(
    "changes",
    [
        {"role_code": "observer", "scope_mode": "all"},
        {"must_change_password": 2},
        {"temporary_password_expires_at": None},
        {"must_change_password": 0},
        {"status": "deleted"},
        {"deleted_at": NOW},
        {"failed_attempts": -1},
        {"username": "Uppercase"},
        {"display_name": "   "},
    ],
)
def test_account_constraints(engine, changes):
    with pytest.raises(sa.exc.IntegrityError):
        with session_factory(engine).begin() as session:
            session.add(user(**changes))


def test_username_reserved_after_soft_delete_and_audit_independent(engine):
    factory = session_factory(engine)
    with factory.begin() as session:
        session.add(user(status="deleted", deleted_at=NOW))
        session.add(
            AuditLog(
                event_id="e1",
                actor_user_id="u1",
                target_user_id="u1",
                action="user.delete",
                resource_type="user",
                resource_id="u1",
                outcome="success",
                request_id="r1",
                occurred_at=NOW,
            )
        )
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.add(user(user_id="u2"))
    with factory.begin() as session:
        session.delete(session.scalar(sa.select(AuthUser)))
    with factory() as session:
        assert session.scalar(sa.select(AuditLog)).target_user_id == "u1"


def test_whole_station_unique_and_user_fk_restrict(engine):
    factory = session_factory(engine)
    with factory.begin() as session:
        session.add(user())
        session.flush()
        session.add(AuthUserScope(user_id="u1", station_id="station"))
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.add(AuthUserScope(user_id="u1", station_id="station", device_root_id=""))
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.delete(session.scalar(sa.select(AuthUser)))
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.add(AuthUserScope(user_id="missing", station_id="station"))


def test_session_hash_unique_and_login_does_not_change_edit_time(engine):
    factory = session_factory(engine)
    with factory.begin() as session:
        session.add(user())
        session.flush()
        session.add(session_row())
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.add(session_row(session_id="s2"))
    with factory.begin() as session:
        account = session.scalar(sa.select(AuthUser))
        account.last_login_at = NOW + timedelta(minutes=1)
    with factory() as session:
        assert session.scalar(sa.select(AuthUser)).updated_at == NOW
        assert session.scalar(sa.select(AuthSession)).token_hash == b"h" * 32


@pytest.mark.parametrize(
    "field,value",
    [
        ("id", 2),
        ("failed_login_limit", 7),
        ("lock_minutes", 9),
        ("idle_timeout_minutes", 31),
        ("password_min_length", 11),
        ("login_notice", "x" * 501),
    ],
)
def test_policy_constraints(engine, field, value):
    with pytest.raises(sa.exc.IntegrityError):
        with session_factory(engine).begin() as session:
            session.add(AuthLoginPolicy(**({"id": 1, "updated_at": NOW} | {field: value})))


def test_cli_initializes_two_distinct_databases_from_one_config(tmp_path, capsys):
    path = tmp_path / "database.yaml"
    path.write_text(
        "database: sqlite\ndatabases:\n  sqlite:\n    url: sqlite:///ems.sqlite3\n", encoding="utf8"
    )
    for target, seed in (("ems", "catalog"), ("ems_web", "policy")):
        args = ["--config", str(path), "--database", target]
        assert main(args + ["init"]) == 0
        assert main(args + ["seed", seed]) == 0
        assert main(args + ["check"]) == 0
        assert main(args + ["seed", "policy" if target == "ems" else "demo"]) == 1
    for target, metadata in (("ems", Base.metadata), ("ems_web", WebBase.metadata)):
        engine = make_engine(sa.URL.create("sqlite", database=str(tmp_path / f"{target}.sqlite3")))
        with engine.connect() as connection:
            assert set(sa.inspect(connection).get_table_names()) == set(metadata.tables) | {
                "alembic_version"
            }
            revision = connection.exec_driver_sql(
                "SELECT version_num FROM alembic_version"
            ).scalar_one()
            assert revision == (BASELINE.revision if target == "ems_web" else "c7b19f2a6d40")
        engine.dispose()
    assert main(["--database", "ems_web", "upgrade", "--sql", "--dialect", "mysql"]) == 0
    sql = capsys.readouterr().out
    assert "CREATE TABLE auth_user" in sql and "CREATE TABLE cfg_device" not in sql


def test_wrong_target_refuses_before_modifying_tables(engine):
    with engine.connect() as connection:
        with pytest.raises(ValueError, match="outside"):
            command.upgrade(migration_config(connection=connection), "head")
        assert set(sa.inspect(connection).get_table_names()) == TABLES | {"alembic_version"}
    engineering = make_engine("sqlite:///:memory:")
    with engineering.begin() as connection:
        command.upgrade(migration_config(connection=connection), "head")
        with pytest.raises(ValueError, match="outside"):
            require_target(connection, "ems_web")
        assert set(sa.inspect(connection).get_table_names()) == set(Base.metadata.tables) | {
            "alembic_version"
        }
    engineering.dispose()


def test_web_autogeneration_cannot_drop_engineering_tables(engine):
    from alembic.autogenerate import produce_migrations
    from alembic.operations.ops import DropTableOp

    with engine.begin() as connection:
        connection.exec_driver_sql("CREATE TABLE cfg_device (id INTEGER PRIMARY KEY)")
        context = MigrationContext.configure(
            connection,
            opts=comparison_options("sqlite", metadata=WebBase.metadata, retired_tables=()),
        )
        generated = produce_migrations(context, WebBase.metadata)
        assert not any(isinstance(op, DropTableOp) for op in generated.upgrade_ops.ops)
        with pytest.raises(ValueError, match="outside"):
            require_target(connection, "ems_web")
