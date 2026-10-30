"""Shared Alembic execution; each database supplies its own metadata and revision directory."""

from alembic import context

from db.config import load_url, make_engine
from db.migrations.render import render_item
from db.schema import comparison_options
from db.targets import require_target, target_metadata, target_retired_tables


def run_environment(database: str) -> None:
    config = context.config
    if config.attributes.get("database_target", database) != database:
        raise ValueError("Alembic environment and selected database target disagree")
    metadata = target_metadata(database)

    def configure(**kwargs):
        dialect = kwargs.get("dialect_name")
        if "connection" in kwargs:
            dialect = kwargs["connection"].dialect.name
        context.configure(
            target_metadata=metadata,
            render_item=render_item,
            **comparison_options(
                dialect, metadata=metadata, retired_tables=target_retired_tables(database)
            ),
            **kwargs,
        )

    def run_migrations(connection):
        configure(connection=connection)
        with context.begin_transaction():
            context.run_migrations()

    def migrate(connection):
        require_target(connection, database)
        if connection.dialect.name == "sqlite" and not connection.in_transaction():
            raw = connection.connection.driver_connection
            raw.execute("PRAGMA foreign_keys=OFF")
            try:
                with connection.begin():
                    run_migrations(connection)
                    if connection.exec_driver_sql("PRAGMA foreign_key_check").first() is not None:
                        raise ValueError(
                            "SQLite migration violates foreign keys; changes rolled back"
                        )
            finally:
                raw.execute("PRAGMA foreign_keys=ON")
        else:
            run_migrations(connection)

    def url():
        return load_url(database="ems_web" if database == "ems_web" else None)

    if context.is_offline_mode():
        dialect = config.attributes.get("dialect") or url().get_backend_name()
        configure(dialect_name=dialect, literal_binds=True)
        with context.begin_transaction():
            context.run_migrations()
    elif config.attributes.get("connection") is not None:
        migrate(config.attributes["connection"])
    else:
        engine = make_engine(url())
        try:
            with engine.connect() as connection:
                migrate(connection)
        finally:
            engine.dispose()
