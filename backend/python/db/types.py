"""Portable column types with narrow MySQL compatibility variants."""

from datetime import datetime

import sqlalchemy as sa
from sqlalchemy.dialects import mysql
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.sql.expression import ColumnElement


def integer(*, unsigned: bool = False, size: str = "integer", boolean: bool = False):
    """Use signed integers portably; retain existing unsigned MySQL storage when applicable."""
    generic = sa.BigInteger() if size == "big" else sa.Integer()
    if size == "big":
        generic = generic.with_variant(sa.Integer(), "sqlite")  # SQLite rowid/autoincrement.
    mysql_type = {
        "big": mysql.BIGINT,
        "small": mysql.SMALLINT,
        "tiny": mysql.TINYINT,
        "integer": mysql.INTEGER,
    }[size]
    kwargs = {"unsigned": unsigned}
    if boolean:
        kwargs["display_width"] = 1
    return generic.with_variant(mysql_type(**kwargs), "mysql")


def enumeration(*values: str, name: str):
    """Persist enum strings on every backend; retain native ENUM for existing MySQL schemas."""
    return sa.Enum(
        *values, name=name, native_enum=False, create_constraint=True, validate_strings=True
    ).with_variant(mysql.ENUM(*values), "mysql")


def timestamp(*, microseconds: bool = True):
    """Store naive timestamps, preserving MySQL microsecond precision."""
    return sa.DateTime().with_variant(mysql.DATETIME(fsp=6 if microseconds else None), "mysql")


class TimestampDefault(ColumnElement[datetime]):
    """Server default with MySQL's existing ON UPDATE behavior where requested."""

    type = sa.DateTime()
    inherit_cache = False

    def __init__(self, on_update: bool = False, microseconds: bool = True):
        self.on_update = on_update
        self.microseconds = microseconds


@compiles(TimestampDefault)
def _timestamp_default(element, compiler, **kw):
    return "CURRENT_TIMESTAMP"


@compiles(TimestampDefault, "mysql")
def _mysql_timestamp_default(element, compiler, **kw):
    value = "CURRENT_TIMESTAMP(6)" if element.microseconds else "CURRENT_TIMESTAMP"
    return value + (" ON UPDATE " + value if element.on_update else "")
