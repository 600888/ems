from pathlib import Path

import pytest
import yaml

from db.config import load_url


@pytest.mark.parametrize(
    "backend,base",
    [
        ("mysql", "mysql://ems:secret@localhost:3307/legacy"),
        ("postgresql", "postgresql://ems:secret@localhost:5433/legacy"),
    ],
)
def test_shared_connection_only_target_name_changes(tmp_path, backend, base):
    path = tmp_path / "database.yaml"
    path.write_text(
        yaml.safe_dump({"database": backend, "databases": {backend: {"url": base}}}),
        encoding="utf8",
    )
    engineering = load_url(path, {}, database="ems")
    web = load_url(path, {}, database="ems_web")
    assert web.database == "ems_web" and engineering.database == "ems"
    assert web.set(database="ems") == engineering
    assert load_url(path, {}).database == "legacy"


def test_cli_target_overrides_environment_database_name():
    env = {"EMS_DATABASE_URL": "mysql://ems:secret@host/old"}
    assert load_url(environ=env, database="ems_web").database == "ems_web"


def test_sqlite_targets_are_separate_siblings(tmp_path):
    path = tmp_path / "database.yaml"
    path.write_text(
        "database: sqlite\ndatabases:\n  sqlite:\n    url: sqlite:///local.sqlite3\n",
        encoding="utf8",
    )
    for name in ("ems", "ems_web"):
        assert Path(load_url(path, {}, database=name).database) == tmp_path / f"{name}.sqlite3"
    assert Path(load_url(path, {}).database) == tmp_path / "local.sqlite3"


def test_invalid_target_rejected_before_reading_configuration():
    with pytest.raises(ValueError, match="target"):
        load_url("does-not-exist", {}, database="unknown")
