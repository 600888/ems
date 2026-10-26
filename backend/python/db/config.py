"""Portable database URL, engine and ORM session factories."""

import json
import os
from collections.abc import Mapping
from pathlib import Path
import yaml
from sqlalchemy import URL, Engine, create_engine, event
from sqlalchemy.engine import make_url
from sqlalchemy.orm import sessionmaker


def load_url(path: str | Path | None = None, environ: Mapping[str, str] | None = None) -> URL:
    """Load selected YAML backend; environment URL takes precedence. Legacy JSON remains valid."""
    env = os.environ if environ is None else environ
    if env.get("EMS_DATABASE_URL"):
        return normalize_url(env["EMS_DATABASE_URL"])
    filename = path if path is not None else env.get("EMS_DATABASE_CONFIG")
    if filename is None:
        default = Path(__file__).resolve().parents[1] / "database.yaml"
        filename = default if default.is_file() else None
    settings = {}
    if filename:
        filename = Path(filename).resolve()
        try:
            source = filename.read_text(encoding="utf-8-sig")
            settings = (
                json.loads(source) if filename.suffix.lower() == ".json" else yaml.safe_load(source)
            )
        except (OSError, ValueError, yaml.YAMLError) as exc:
            raise ValueError("Cannot read database YAML/JSON configuration") from exc
        if not isinstance(settings, dict):
            raise ValueError("Database configuration must be a mapping")
        if "databases" in settings:
            return _yaml_url(settings, filename, env)
        if filename.suffix.lower() in (".yaml", ".yml"):
            raise ValueError("YAML requires database and databases fields")
    if "url" in settings:
        return normalize_url(settings["url"])
    for key in ("host", "port", "user", "password", "database"):
        if "EMS_MYSQL_" + key.upper() in env:
            settings[key] = env["EMS_MYSQL_" + key.upper()]
    for key, default in (("host", "127.0.0.1"), ("user", ""), ("database", ""), ("password", "")):
        value = settings.setdefault(key, default)
        if not isinstance(value, str) or (key != "password" and not value):
            raise ValueError(f"Invalid MySQL {key}; alternatively set EMS_DATABASE_URL")
    port = settings.get("port", 3306)
    if (
        isinstance(port, bool)
        or not (
            isinstance(port, int) or (isinstance(port, str) and port.isascii() and port.isdigit())
        )
        or not 0 < int(port) <= 65535
    ):
        raise ValueError("Invalid MySQL port")
    return URL.create(
        "mysql+pymysql",
        username=settings["user"],
        password=settings["password"],
        host=settings["host"],
        port=int(port),
        database=settings["database"],
        query={"charset": "utf8mb4"},
    )


def _yaml_url(settings: dict, filename: Path, env: Mapping[str, str]) -> URL:
    backend = settings.get("database")
    if backend not in ("sqlite", "mysql", "postgresql"):
        raise ValueError("database must be sqlite, mysql or postgresql")
    databases = settings["databases"]
    section = databases.get(backend) if isinstance(databases, dict) else None
    if not isinstance(section, dict):
        raise ValueError(f"Missing databases.{backend} configuration")
    try:
        url = make_url(section.get("url", backend + "://"))
    except Exception as exc:
        raise ValueError(f"Invalid databases.{backend}.url") from exc
    if url.get_backend_name() != backend:
        raise ValueError("Selected database and URL backend disagree")
    fields = {}
    if backend != "sqlite":
        for key in ("host", "username", "password", "database"):
            if key in section:
                if not isinstance(section[key], str):
                    raise ValueError(f"Invalid databases.{backend}.{key}")
                fields[key] = section[key]
        if "port" in section:
            port = section["port"]
            if isinstance(port, bool) or not isinstance(port, int) or not 0 < port <= 65535:
                raise ValueError(
                    f"Invalid databases.{backend}.port (use an integer from 1 to 65535)"
                )
            fields["port"] = port
    url = url.set(**fields)
    if backend == "mysql":
        overrides = {}
        for key in ("host", "username", "password", "database", "port"):
            variable = "EMS_MYSQL_" + ("USER" if key == "username" else key.upper())
            if variable in env:
                value = env[variable]
                if key == "port":
                    if not value.isascii() or not value.isdigit() or not 0 < int(value) <= 65535:
                        raise ValueError("Invalid EMS_MYSQL_PORT")
                    value = int(value)
                overrides[key] = value
        url = url.set(**overrides)
    url = normalize_url(url)
    if backend == "sqlite" and url.database not in (None, "", ":memory:"):
        if not url.query.get("uri"):
            database_path = Path(url.database)
            if not database_path.is_absolute():
                url = url.set(database=str((filename.parent / database_path).resolve()))
    return url


def normalize_url(value: str | URL) -> URL:
    """Select a supported driver without echoing credentials in invalid-URL errors."""
    try:
        url = make_url(value)
    except Exception as exc:
        raise ValueError("Invalid database URL") from exc
    drivers = {
        "sqlite": "sqlite+pysqlite",
        "mysql": "mysql+pymysql",
        "postgresql": "postgresql+psycopg",
    }
    url = url.set(drivername=drivers.get(url.drivername, url.drivername))
    if url.drivername not in drivers.values():
        raise ValueError("Use sqlite+pysqlite, mysql+pymysql or postgresql+psycopg")
    if url.get_backend_name() != "sqlite" and (not url.database or not url.username):
        raise ValueError("Database URL requires a username and database")
    if url.get_backend_name() == "mysql" and "charset" not in url.query:
        url = url.update_query_dict({"charset": "utf8mb4"})
    return url


def make_engine(url: str | URL) -> Engine:
    """Create an engine with SQLite foreign keys enabled for every pooled connection."""
    url = normalize_url(url)
    kwargs = {"pool_pre_ping": True, "hide_parameters": True}
    if url.get_backend_name() == "mysql":
        kwargs["connect_args"] = {"connect_timeout": 10, "read_timeout": 300, "write_timeout": 300}
    elif url.get_backend_name() == "postgresql":
        kwargs["connect_args"] = {"connect_timeout": 10}
    try:
        engine = create_engine(url, **kwargs)
    except ModuleNotFoundError as exc:
        extra = {"mysql": "mysql", "postgresql": "postgresql"}.get(url.get_backend_name())
        if extra and exc.name in ("pymysql", "psycopg", "psycopg_binary", "psycopg_c"):
            raise ValueError(
                f"Missing {url.get_backend_name()} driver module '{exc.name}'. "
                f"From backend/python run: uv sync --extra {extra}; "
                f'or: python -m pip install -e ".[{extra}]"'
            ) from exc
        raise
    if url.get_backend_name() == "sqlite":

        @event.listens_for(engine, "connect")
        def _foreign_keys(dbapi_connection, connection_record):
            # Let SQLAlchemy explicitly BEGIN, including DDL. sqlite3's legacy mode otherwise
            # leaves CREATE/DROP outside transactions even inside engine.begin().
            dbapi_connection.isolation_level = None
            cursor = dbapi_connection.cursor()
            cursor.execute("PRAGMA foreign_keys=ON")
            cursor.close()

        @event.listens_for(engine, "begin")
        def _begin(connection):
            connection.exec_driver_sql("BEGIN")

    return engine


def session_factory(engine: Engine) -> sessionmaker:
    """Callers own the session and its transaction scope."""
    return sessionmaker(engine, expire_on_commit=False)
