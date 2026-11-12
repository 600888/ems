"""按业务分层的公开导入及已有导入路径兼容性。"""

import importlib
from pathlib import Path

import dlt698 as d
from dlt698 import (
    app,
    codec,
    common,
    expert,
    model,
    protocol,
    security,
    service,
    session,
    standard,
    transport,
)


def test_business_packages_and_compatibility_identity():
    for package in (
        app,
        codec,
        common,
        expert,
        model,
        protocol,
        security,
        service,
        session,
        standard,
        transport,
    ):
        assert Path(package.__file__).name == "__init__.py"
    for legacy, canonical, names in (
        ("client", "app.client", ("Client", "ReadResult")),
        ("server", "app.server", ("Server",)),
        ("async_client", "app.async_client", ("AsyncClient",)),
        ("errors", "common.errors", ("Dlt698Error", "DarError", "_from_native")),
    ):
        old = importlib.import_module("dlt698." + legacy)
        new = importlib.import_module("dlt698." + canonical)
        for name in names:
            assert getattr(old, name) is getattr(new, name)
    assert d.AsyncServer is app.AsyncServer
    assert expert.ObjectRegistry is service.ObjectRegistry
    assert expert.SecurityBackend is security.SecurityBackend
    assert expert.Engine is session.Engine
    assert expert.TransBridge is transport.TransBridge
    assert protocol.GetRequest is protocol.apdu.GetRequest
    assert protocol.Frame is protocol.link.Frame is codec.Frame


def test_explicit_public_exports_exist():
    for package in (
        d,
        app,
        codec,
        common,
        expert,
        model,
        protocol,
        security,
        service,
        session,
        standard,
        transport,
    ):
        assert len(package.__all__) == len(set(package.__all__))
        for name in package.__all__:
            assert hasattr(package, name), (package.__name__, name)
