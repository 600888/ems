"""Engineering database Alembic environment."""

from db.migrations.runtime import run_environment

run_environment("ems")
