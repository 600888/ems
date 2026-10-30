"""Deployment CLI. Applications use ORM sessions rather than invoking this at startup."""

import argparse
import sys
from pathlib import Path

import sqlalchemy as sa
from alembic import command
from alembic.config import Config
from alembic.util.exc import CommandError

from .config import load_url, make_engine, session_factory
from .schema import schema_differences
from .seeds import apply_seed
from .targets import target_directory, target_metadata, target_retired_tables, require_target


def migration_config(*, connection=None, dialect=None, output=None, database="ems") -> Config:
    """Locate Alembic independently of the current working directory."""
    config = Config(str(target_directory(database) / "alembic.ini"), stdout=output or sys.stdout)
    config.attributes["database_target"] = database
    if output is not None:
        config.output_buffer = output
    if connection is not None:
        config.attributes["connection"] = connection
    if dialect is not None:
        config.attributes["dialect"] = dialect
    return config


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="EMS SQLAlchemy ORM / Alembic schema management")
    result.add_argument(
        "--config",
        type=Path,
        help="migration YAML (default: backend/python/database.yaml); legacy JSON is supported",
    )
    result.add_argument(
        "--database",
        choices=("ems", "ems_web"),
        help="select engineering or Web models and target database (default: engineering)",
    )
    actions = result.add_subparsers(dest="action", required=True)
    actions.add_parser("init", help="apply schema migrations to head")
    for name, default in (("upgrade", "head"), ("downgrade", None)):
        action = actions.add_parser(name)
        action.add_argument("revision", nargs="?" if default else None, default=default)
        action.add_argument("--sql", action="store_true", help="emit SQL without connecting")
        action.add_argument("--dialect", choices=("sqlite", "mysql", "postgresql"))
    actions.add_parser("current")
    actions.add_parser("history")
    actions.add_parser("check", help="compare ORM schema to database (read only)")
    seed = actions.add_parser("seed", help="explicitly insert catalog or development demo data")
    seed.add_argument("kind", choices=("catalog", "demo", "policy"))
    revision = actions.add_parser("revision", help="create a migration script for review")
    revision.add_argument("-m", "--message", required=True)
    revision.add_argument("--autogenerate", action="store_true")
    return result


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    database = args.database or "ems"
    try:
        if args.action == "seed" and (
            (database == "ems_web" and args.kind != "policy")
            or (database == "ems" and args.kind == "policy")
        ):
            raise ValueError("ems supports catalog/demo seeds; ems_web supports only policy")
        if args.action == "history":
            command.history(migration_config(database=database))
            return 0
        if args.action in ("upgrade", "downgrade") and args.sql:
            config = migration_config(
                dialect=args.dialect
                or load_url(args.config, database=args.database).get_backend_name(),
                database=database,
            )
            getattr(command, args.action)(config, args.revision, sql=True)
            return 0
        if args.action == "revision" and not args.autogenerate:
            command.revision(migration_config(database=database), message=args.message)
            return 0
        engine = make_engine(load_url(args.config, database=args.database))
        try:
            with engine.connect() as connection:
                require_target(connection, database)
                config = migration_config(connection=connection, database=database)
                if args.action in ("init", "upgrade"):
                    command.upgrade(config, "head" if args.action == "init" else args.revision)
                elif args.action == "downgrade":
                    command.downgrade(config, args.revision)
                elif args.action == "current":
                    command.current(config)
                elif args.action == "revision":
                    command.revision(config, message=args.message, autogenerate=True)
                elif args.action == "check":
                    differences = schema_differences(
                        connection,
                        metadata=target_metadata(database),
                        retired_tables=target_retired_tables(database),
                    )
                    if differences:
                        print("\n".join(differences), file=sys.stderr)
                        return 1
                    print(
                        "Schema matches ORM for compared objects. Review CHECK/expression changes manually."
                    )
                elif args.action == "seed":
                    with connection.begin():
                        with session_factory(connection).begin() as session:
                            if database == "ems_web":
                                from web_db.seeds import apply_seed as apply_web_seed

                                apply_web_seed(session, args.kind)
                            else:
                                apply_seed(session, args.kind)
        finally:
            engine.dispose()
        return 0
    except (ValueError, OSError, ImportError, CommandError, sa.exc.SQLAlchemyError) as exc:
        # Driver exceptions/URLs may contain credentials; keep detailed exceptions out of CLI output.
        if isinstance(exc, ModuleNotFoundError):
            message = f"Missing Python module '{exc.name or 'unknown'}' in {sys.executable}"
        else:
            message = (
                str(exc) if isinstance(exc, (ValueError, CommandError)) else type(exc).__name__
            )
        print(f"Database operation failed: {message}", file=sys.stderr)
        return 1
