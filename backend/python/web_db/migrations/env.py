"""Web database Alembic environment with its own model registry."""

from db.migrations.runtime import run_environment

run_environment("ems_web")
