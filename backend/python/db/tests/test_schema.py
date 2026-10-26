from types import SimpleNamespace

import pytest
import sqlalchemy as sa

from db.schema import compare_numeric_defaults


@pytest.mark.parametrize(
    "actual,expected,different",
    [
        ("'1.00000000'", "1", False),
        ("'0.00000000'", "0", False),
        ("(-(9999999))", "-9999999", False),
        ("(-(9999999.0))", "-9999999.0", False),
        ("'1.00000001'", "1", True),
        ("'9007199254740993'", "9007199254740992", True),
        (None, "1", None),
        ("(1 + 2)", "3", None),
        ("rand()", "1", None),
    ],
)
def test_mysql_numeric_default_comparison(actual, expected, different):
    context = SimpleNamespace(dialect=SimpleNamespace(name="mysql"))
    column = sa.Column("number", sa.Numeric(20, 8))
    assert compare_numeric_defaults(context, column, column, actual, None, expected) is different


def test_string_defaults_and_other_dialects_use_alembic_comparison():
    context = SimpleNamespace(dialect=SimpleNamespace(name="mysql"))
    column = sa.Column("text", sa.String(32))
    assert compare_numeric_defaults(context, column, column, "'1.0'", None, "'1'") is None
    context.dialect.name = "sqlite"
    column = sa.Column("number", sa.Numeric(20, 8))
    assert compare_numeric_defaults(context, column, column, "1.0", None, "1") is None
