import importlib
import io
import re
import shutil
from pathlib import Path

import pytest
import sqlalchemy as sa
from alembic import command
from alembic.migration import MigrationContext
from alembic.operations import Operations

from db.cli import main, migration_config
from db.config import make_engine, session_factory
from db.models import (
    Base,
    Binding,
    Channel,
    Device,
    DeviceType,
    PointBase,
    TypePoint,
    YcPoint,
    YxPoint,
    YkPoint,
    YtPoint,
    LegacyDevice,
)
from db.schema import schema_differences
from db.seeds import apply_seed

BASELINE = importlib.import_module("db.migrations.versions.c7b19f2a6d40_initial_schema")


@pytest.fixture
def engine():
    engine = make_engine("sqlite:///:memory:")
    with engine.begin() as connection:
        command.upgrade(migration_config(connection=connection), "head")
    yield engine
    engine.dispose()


def ddl_statements(text):
    result = set()
    for sql in text.split(";"):
        sql = re.sub(r"\s+", " ", sql.strip())
        if not sql or sql.startswith("--"):
            continue
        if not sql.startswith("CREATE TABLE"):
            result.add(sql)
            continue
        # Constraint ordering and MySQL table-option ordering do not change schema semantics.
        start = sql.index("(")
        depth, quoted, part = 0, False, start + 1
        fields = []
        for i in range(start + 1, len(sql)):
            char = sql[i]
            if char == "'":
                quoted = not quoted
            if not quoted:
                if char == "(":
                    depth += 1
                elif char == ")":
                    if depth == 0:
                        fields.append(sql[part:i].strip())
                        tail = sql[i + 1 :]
                        break
                    depth -= 1
                elif char == "," and depth == 0:
                    fields.append(sql[part:i].strip())
                    part = i + 1
        options = tuple(
            sorted(re.findall(r"(?:ENGINE|CHARSET|COMMENT)=(?:'(?:''|[^'])*'|\w+)", tail))
        )
        result.add((sql[:start].strip(), tuple(sorted(fields)), options))
    return result


@pytest.mark.parametrize("dialect", ["sqlite", "mysql", "postgresql"])
def test_frozen_migration_matches_orm_ddl_for_each_database(dialect):
    expected = []
    mock = sa.create_mock_engine(
        dialect + "://",
        lambda ddl, *a, **kw: expected.append(str(ddl.compile(dialect=mock.dialect))),
    )
    Base.metadata.create_all(mock)
    buffer = io.StringIO()
    ctx = MigrationContext.configure(
        dialect=mock.dialect, opts={"as_sql": True, "output_buffer": buffer}
    )
    with Operations.context(ctx):
        BASELINE.upgrade()
    assert ddl_statements(buffer.getvalue()) == ddl_statements(";\n".join(expected))


def test_all_models_are_mapped_and_four_point_tables_share_base():
    assert len(Base.registry.mappers) == len(Base.metadata.tables) == 25
    for model, table in (
        (YcPoint, "cfg_point_yc"),
        (YxPoint, "cfg_point_yx"),
        (YkPoint, "cfg_point_yk"),
        (YtPoint, "cfg_point_yt"),
    ):
        assert issubclass(model, PointBase)
        assert sa.inspect(model).local_table.name == table


def test_upgrade_check_downgrade_and_reupgrade(engine):
    with engine.begin() as connection:
        assert schema_differences(connection) == []
        assert connection.exec_driver_sql("PRAGMA foreign_keys").scalar_one() == 1
        command.downgrade(migration_config(connection=connection), "base")
        assert set(sa.inspect(connection).get_table_names()) == {"alembic_version"}
        command.upgrade(migration_config(connection=connection), "head")
        assert schema_differences(connection) == []


def test_catalog_seed_idempotent_and_preserves_existing_station(engine):
    factory = session_factory(engine)
    with factory.begin() as session:
        session.add(
            Device(
                device_id="real_station",
                parent_device_id="",
                node_kind="station",
                code="real",
                name="工程站",
                path="/",
                depth=0,
            )
        )
    for _ in range(2):
        with factory.begin() as session:
            apply_seed(session, "catalog")
    with factory() as session:
        assert session.scalar(sa.select(sa.func.count()).select_from(DeviceType)) == 11
        assert session.scalar(sa.select(sa.func.count()).select_from(TypePoint)) == 47
        assert session.scalars(sa.select(Device.device_id)).all() == ["real_station"]
        point = session.get(TypePoint, ("pcs", "pcs-v1", "active_power"))
        assert point.capability_codes_json == ["monitor_power", "set_active_power"]
        assert point.stale_after_ms == 0


def test_demo_seed_uses_generated_ids_and_preserves_user_rows(engine):
    factory = session_factory(engine)
    with factory.begin() as session:
        session.add(LegacyDevice(code="existing", name="User device"))
    for _ in range(2):
        with factory.begin() as session:
            apply_seed(session, "demo")
    with factory() as session:
        assert session.scalar(sa.select(sa.func.count()).select_from(LegacyDevice)) == 2
        demo = session.scalar(sa.select(LegacyDevice).where(LegacyDevice.code == "PCS_001"))
        assert demo.id != 1
        assert session.get(LegacyDevice, 1).name == "User device"


def test_seed_failure_rolls_back(engine):
    factory = session_factory(engine)
    with pytest.raises(RuntimeError):
        with factory.begin() as session:
            apply_seed(session, "catalog")
            raise RuntimeError("failure before commit")
    with factory() as session:
        assert session.scalar(sa.select(sa.func.count()).select_from(Device)) == 0
        assert session.scalar(sa.select(sa.func.count()).select_from(DeviceType)) == 0


def test_orm_point_constraints_and_delete_cascade(engine):
    factory = session_factory(engine)
    with factory.begin() as session:
        apply_seed(session, "catalog")
        session.add(Channel(channel_id="ch", code="ch", protocol="modbus", conn_mode="tcp_client"))
        session.flush()
        session.add(
            Binding(binding_id="binding", device_id="station", channel_id="ch", can_collect=1)
        )
        session.flush()
        session.add_all(
            [
                YcPoint(
                    point_id="p1",
                    owner_device_id="station",
                    binding_id="binding",
                    code="p1",
                    wire_type="f32",
                ),
                YcPoint(
                    point_id="p2",
                    owner_device_id="station",
                    binding_id="binding",
                    code="p2",
                    wire_type="f32",
                ),
            ]
        )
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.add(
                YcPoint(
                    point_id="bad",
                    owner_device_id="station",
                    binding_id="missing",
                    code="bad",
                    wire_type="f32",
                )
            )
    with pytest.raises(sa.exc.IntegrityError):
        with factory.begin() as session:
            session.add(
                YcPoint(
                    point_id="disabled",
                    owner_device_id="station",
                    binding_id="binding",
                    code="disabled",
                    wire_type="f32",
                    enabled=0,
                )
            )
    with factory.begin() as session:
        session.execute(sa.delete(Device).where(Device.device_id == "station"))
    with factory() as session:
        assert session.scalar(sa.select(sa.func.count()).select_from(YcPoint)) == 0


def test_schema_check_detects_missing_index_and_column(engine):
    with engine.begin() as connection:
        connection.exec_driver_sql("DROP INDEX uq_yc_semantic")
        assert any("uq_yc_semantic" in diff for diff in schema_differences(connection))
        connection.exec_driver_sql("ALTER TABLE device ADD COLUMN unexpected INTEGER")
        assert any("unexpected" in diff for diff in schema_differences(connection))


def test_cli_yaml_init_seed_check_and_offline(tmp_path, capsys):
    path = tmp_path / "database.yaml"
    path.write_text(
        "database: sqlite\ndatabases:\n  sqlite:\n    url: sqlite:///cli.sqlite3\n",
        encoding="utf-8",
    )
    assert main(["--config", str(path), "init"]) == 0
    assert main(["--config", str(path), "seed", "catalog"]) == 0
    assert main(["--config", str(path), "check"]) == 0
    assert (tmp_path / "cli.sqlite3").exists()
    assert main(["upgrade", "--sql", "--dialect", "postgresql"]) == 0
    assert "CREATE TABLE cfg_device" in capsys.readouterr().out


def test_sqlite_batch_rebuild_preserves_children(engine, tmp_path):
    with session_factory(engine).begin() as session:
        apply_seed(session, "catalog")
        session.add(Channel(channel_id="ch", code="ch", protocol="modbus", conn_mode="tcp_client"))
        session.flush()
        session.add(Binding(binding_id="b", device_id="station", channel_id="ch", can_collect=1))
    migration_dir = tmp_path / "migrations"
    shutil.copytree(Path(__file__).resolve().parents[1] / "migrations", migration_dir)
    (migration_dir / "versions" / "test_batch.py").write_text(
        "from alembic import op\n"
        "import sqlalchemy as sa\n"
        "revision = 'test_batch'\n"
        "down_revision = 'c7b19f2a6d40'\n"
        "def upgrade():\n"
        "    with op.batch_alter_table('cfg_device', recreate='always') as batch:\n"
        "        batch.add_column(sa.Column('batch_test', sa.String(8), nullable=True))\n",
        encoding="utf-8",
    )
    with engine.connect() as connection:
        config = migration_config(connection=connection)
        config.set_main_option("script_location", str(migration_dir))
        command.upgrade(config, "head")
        assert connection.exec_driver_sql("PRAGMA foreign_keys").scalar_one() == 1
        assert connection.exec_driver_sql("SELECT COUNT(*) FROM cfg_binding").scalar_one() == 1
        assert connection.exec_driver_sql("PRAGMA foreign_key_check").all() == []


def test_sqlite_ddl_rolls_back(engine):
    with pytest.raises(RuntimeError):
        with engine.begin() as connection:
            connection.exec_driver_sql("CREATE TABLE rollback_ddl (id INTEGER)")
            raise RuntimeError("abort migration")
    assert "rollback_ddl" not in sa.inspect(engine).get_table_names()
