"""Select independent schemas and migration histories through the single ems-db CLI."""

from contextlib import nullcontext
from pathlib import Path

import sqlalchemy as sa

from .models import Base
from .schema import RETIRED_TABLES, require_supported


def target_metadata(database: str):
    if database == "ems":
        return Base.metadata
    if database == "ems_web":
        from web_db.models import WebBase

        return WebBase.metadata
    raise ValueError("database target must be ems or ems_web")


def target_directory(database: str) -> Path:
    if database == "ems":
        return Path(__file__).resolve().parent
    if database == "ems_web":
        import web_db

        return Path(web_db.__file__).resolve().parent
    raise ValueError("database target must be ems or ems_web")


def target_retired_tables(database: str):
    return RETIRED_TABLES if database == "ems" else frozenset()


def require_target(connection: sa.Connection, database: str) -> None:
    """Reject mixed databases before DDL, comparison or seeds; leave caller transactions alone."""
    require_supported(connection)
    expected = set(target_metadata(database).tables)
    other = "ems_web" if database == "ems" else "ems"
    forbidden = set(target_metadata(other).tables)
    if database == "ems_web":
        forbidden |= RETIRED_TABLES
    actual_name = connection.engine.url.database
    if connection.dialect.name != "sqlite":
        if actual_name == other:
            raise ValueError(f"Selected {database} models but connection points to {other}")
    elif actual_name and actual_name != ":memory:":
        if Path(actual_name).name == other + ".sqlite3":
            raise ValueError(f"Selected {database} models but SQLite file belongs to {other}")
    # Inspector starts an implicit read transaction. Finish our own before Alembic begins DDL.
    with nullcontext() if connection.in_transaction() else connection.begin():
        tables = set(sa.inspect(connection).get_table_names())
        if tables & forbidden or (
            database == "ems_web" and tables - expected - {"alembic_version"}
        ):
            raise ValueError(f"Database contains tables outside the selected {database} model set")
