"""Alembic environment shared by SQLite, MySQL and PostgreSQL."""

from alembic import context
from db.config import load_url, make_engine
from db.models import Base
from db.migrations.render import render_item
from db.schema import comparison_options, require_supported

config = context.config


def configure(**kwargs):
    dialect = kwargs.get("dialect_name")
    if "connection" in kwargs:
        dialect = kwargs["connection"].dialect.name
    context.configure(
        target_metadata=Base.metadata,
        render_item=render_item,
        **comparison_options(dialect),
        **kwargs,
    )


def migrate(connection):
    require_supported(connection)
    if connection.dialect.name == "sqlite" and not connection.in_transaction():
        # Batch table rebuilds must not execute DROP TABLE while FK cascades are active.
        # Toggle outside a transaction, rebuild atomically, then validate before commit.
        raw = connection.connection.driver_connection
        raw.execute("PRAGMA foreign_keys=OFF")
        try:
            with connection.begin():
                run_migrations(connection)
                if connection.exec_driver_sql("PRAGMA foreign_key_check").first() is not None:
                    raise ValueError("SQLite migration violates foreign keys; changes rolled back")
        finally:
            raw.execute("PRAGMA foreign_keys=ON")
    else:
        # An injected active transaction belongs to its caller, including SQLite FK policy.
        run_migrations(connection)


def run_migrations(connection):
    configure(connection=connection)
    with context.begin_transaction():
        context.run_migrations()


if context.is_offline_mode():
    dialect = config.attributes.get("dialect") or load_url().get_backend_name()
    configure(dialect_name=dialect, literal_binds=True)
    with context.begin_transaction():
        context.run_migrations()
elif config.attributes.get("connection") is not None:
    migrate(config.attributes["connection"])
else:
    engine = make_engine(load_url())
    try:
        with engine.connect() as connection:
            migrate(connection)
    finally:
        engine.dispose()
