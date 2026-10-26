import json

import pytest
import yaml

from db.config import load_url, normalize_url, make_engine


def write_config(tmp_path, backend, section):
    path = tmp_path / "database.yaml"
    path.write_text(
        yaml.safe_dump({"database": backend, "databases": {backend: section}}), encoding="utf-8"
    )
    return path


@pytest.mark.parametrize(
    "backend,port,driver",
    [("mysql", 3307, "mysql+pymysql"), ("postgresql", 5433, "postgresql+psycopg")],
)
def test_yaml_backend_fields_and_password_characters(tmp_path, backend, port, driver):
    path = write_config(
        tmp_path,
        backend,
        {
            "url": backend + "://",
            "host": "db.example",
            "port": port,
            "username": "ems",
            "password": "p@ss:/#?%",
            "database": "ems_test",
        },
    )
    url = load_url(path, {})
    assert (url.drivername, url.host, url.port, url.username, url.password, url.database) == (
        driver,
        "db.example",
        port,
        "ems",
        "p@ss:/#?%",
        "ems_test",
    )


def test_sqlite_relative_to_yaml_and_not_cwd(tmp_path, monkeypatch):
    path = write_config(tmp_path, "sqlite", {"url": "sqlite:///project.sqlite3"})
    monkeypatch.chdir(tmp_path.parent)
    assert load_url(path, {}).database == str(tmp_path / "project.sqlite3")


def test_default_migration_yaml_is_independent_of_cwd(tmp_path, monkeypatch):
    migration_dir = tmp_path / "python"
    migration_dir.mkdir()
    write_config(migration_dir, "sqlite", {"url": "sqlite:///ems.sqlite3"})
    monkeypatch.setattr("db.config.__file__", str(migration_dir / "db" / "config.py"))
    write_config(tmp_path, "sqlite", {"url": "sqlite:///:memory:"})
    monkeypatch.chdir(tmp_path)
    assert load_url(environ={}).database == str(migration_dir / "ems.sqlite3")


def test_full_url_and_explicit_fields(tmp_path):
    path = write_config(
        tmp_path,
        "postgresql",
        {
            "url": "postgresql://old:old@host:5432/old",
            "username": "new",
            "password": "new@password",
            "port": 5434,
        },
    )
    url = load_url(path, {})
    assert (url.host, url.database, url.username, url.password, url.port) == (
        "host",
        "old",
        "new",
        "new@password",
        5434,
    )


def test_environment_precedence_and_legacy_json(tmp_path):
    path = tmp_path / "db.json"
    path.write_text(
        json.dumps(
            {"host": "old", "user": "ems", "database": "ems", "port": 3306, "password": "a@b"}
        ),
        encoding="utf-8",
    )
    url = load_url(path, {"EMS_MYSQL_HOST": "new", "EMS_MYSQL_PORT": "3308"})
    assert (url.host, url.port, url.password) == ("new", 3308, "a@b")
    assert (
        load_url(path, {"EMS_DATABASE_URL": "sqlite:///:memory:"}).drivername == "sqlite+pysqlite"
    )


def test_mysql_environment_overrides_yaml(tmp_path):
    path = write_config(tmp_path, "mysql", {"url": "mysql://user:password@host/db"})
    url = load_url(
        path, {"EMS_MYSQL_USER": "new", "EMS_MYSQL_PASSWORD": "p@:/", "EMS_MYSQL_PORT": "3309"}
    )
    assert (url.username, url.password, url.port) == ("new", "p@:/", 3309)


@pytest.mark.parametrize("port", [0, 65536, -1, True, "3306", None])
def test_yaml_invalid_port(tmp_path, port):
    path = write_config(tmp_path, "mysql", {"url": "mysql://user@host/db", "port": port})
    with pytest.raises(ValueError):
        load_url(path, {})


@pytest.mark.parametrize(
    "source",
    [
        "[]",
        "database: unknown\ndatabases: {}",
        "database: sqlite",
        "database: mysql\ndatabases:\n  mysql:\n    url: sqlite://",
        "!!python/object/apply:os.system ['echo unsafe']",
    ],
)
def test_invalid_yaml_rejected(tmp_path, source):
    path = tmp_path / "database.yaml"
    path.write_text(source, encoding="utf-8")
    with pytest.raises(ValueError):
        load_url(path, {})


def test_invalid_url_errors_do_not_echo_password():
    with pytest.raises(ValueError) as error:
        normalize_url("mysql://user:secret@host:bad-port/db")
    assert "secret" not in str(error.value)


@pytest.mark.parametrize("backend,module", [("mysql", "pymysql"), ("postgresql", "psycopg")])
def test_missing_database_driver_has_install_hint_without_credentials(monkeypatch, backend, module):
    def missing_driver(*args, **kwargs):
        raise ModuleNotFoundError(f"No module named '{module}'", name=module)

    monkeypatch.setattr("db.config.create_engine", missing_driver)
    with pytest.raises(ValueError) as error:
        make_engine(f"{backend}://ems:secret@localhost/ems")
    message = str(error.value)
    assert module in message
    assert f"uv sync --extra {backend}" in message
    assert f".[{backend}]" in message
    assert "secret" not in message
