"""用真实 libclang 核对跨目标的构造签名，避免初始化实现影响公开契约。"""

import importlib
import json
from pathlib import Path

import pytest
from clang import cindex

ROOT = Path(__file__).resolve().parents[3]

SOURCE = """
namespace sample {
struct Error {};
template <class T>
class Result {
public:
    Result(T value) : state_(value) {}
    Result(Error error) : state_{} {}
    int value(int amount = 7) const noexcept { return amount; }
private:
    T state_;
};
class Data {
public:
    template <class T>
    Data(T value) noexcept : value_(static_cast<int>(value)) {}
private:
    int value_;
};
class RecordData {
public:
    template <class T>
    RecordData(T value);
private:
    int value_;
};
template <class T>
RecordData::RecordData(T value) : value_{static_cast<int>(value)} {}
int select(int value = 7) noexcept { return value; }
}
"""


@pytest.fixture
def ast_tool(monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / "python/tools"))
    return importlib.import_module("ast_inventory")


@pytest.fixture(params=["x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu"])
def target(request):
    return request.param


def signatures(tool, target, source=SOURCE):
    path = "signature-test.cpp"
    unit = cindex.Index.create().parse(
        path,
        args=["-x", "c++", "-std=c++17", "--target=" + target, "-nostdinc", "-nostdinc++"],
        unsaved_files=[(path, source)],
    )
    assert not [d for d in unit.diagnostics if d.severity >= d.Error]
    function_kinds = {
        cindex.CursorKind.CONSTRUCTOR,
        cindex.CursorKind.CXX_METHOD,
        cindex.CursorKind.FUNCTION_TEMPLATE,
        cindex.CursorKind.FUNCTION_DECL,
    }
    return {
        tool.declaration_signature(cursor)
        for cursor in unit.cursor.walk_preorder()
        if cursor.kind in function_kinds
    }


def test_constructor_initializers_are_not_signatures(ast_tool, target):
    actual = signatures(ast_tool, target)
    assert {
        "Result ( T value )",
        "Result ( Error error )",
        "template < class T > Data ( T value ) noexcept",
        "template < class T > RecordData :: RecordData ( T value )",
    } <= actual
    assert all("state_" not in value and "value_" not in value for value in actual)


def test_default_arguments_and_qualifiers_remain_in_signatures(ast_tool, target):
    actual = signatures(ast_tool, target)
    assert "int value ( int amount = 7 ) const noexcept" in actual
    assert "int select ( int value = 7 ) noexcept" in actual
    changed = signatures(ast_tool, target, SOURCE.replace("amount = 7", "amount = 8"))
    assert changed != actual


def test_function_bodies_do_not_change_signatures(ast_tool, target):
    actual = signatures(ast_tool, target)
    changed = signatures(ast_tool, target, SOURCE.replace("return amount;", "return amount + 1;"))
    assert changed == actual


def test_inventory_reports_real_api_changes(ast_tool, tmp_path, monkeypatch, capsys):
    reviewed = {
        "cpp": "sample::select",
        "kind": "FUNCTION_DECL",
        "signature": "int select ( int value = 7 ) noexcept",
        "header": "cpp/include/sample.hpp",
    }
    path = tmp_path / "python/api-map.json"
    path.parent.mkdir()
    path.write_text(json.dumps({"symbols": [reviewed]}), encoding="utf-8")
    monkeypatch.setattr(ast_tool, "ROOT", tmp_path)
    monkeypatch.setattr(
        ast_tool,
        "inventory",
        lambda includes: [{**reviewed, "signature": "int select ( int value = 8 ) noexcept"}],
    )
    monkeypatch.setattr("sys.argv", ["ast_inventory.py"])
    with pytest.raises(SystemExit, match="Public C\\+\\+ AST differs"):
        ast_tool.main()
    error = capsys.readouterr().err
    assert "--- reviewed python/api-map.json" in error
    assert "+++ current public C++ AST" in error
    assert "sample::select" in error
    assert '-    "signature": "int select ( int value = 7 ) noexcept"' in error
    assert '+    "signature": "int select ( int value = 8 ) noexcept"' in error
