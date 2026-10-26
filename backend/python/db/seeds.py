"""Explicit ORM seeds with the same behavior on all supported databases."""

import json
import sqlalchemy as sa
from sqlalchemy.orm import Session
from .models import Base, Device
from .seed_data import CATALOG, DEMO

MODELS = {mapper.local_table.name: mapper.class_ for mapper in Base.registry.mappers}


def apply_seed(session: Session, kind: str) -> None:
    """Upsert catalog rows or insert absent demo rows in the caller's transaction.

    Run deployment tooling serially. Existing station and demo rows are preserved.
    """
    if kind not in ("catalog", "demo"):
        raise ValueError("Unknown seed kind")
    if (
        kind == "catalog"
        and session.scalar(sa.select(Device.id).where(Device.node_kind == "station").limit(1))
        is None
    ):
        session.add(
            Device(
                device_id="station",
                parent_device_id="",
                node_kind="station",
                code="station",
                name="储能站",
                path="/",
                depth=0,
                enabled=1,
            )
        )
    session.flush()
    for name, rows, updates in CATALOG if kind == "catalog" else DEMO:
        model = MODELS[name]
        mapper = sa.inspect(model)
        for row in rows:
            values = {
                k: json.loads(v) if isinstance(mapper.columns[k].type, sa.JSON) else v
                for k, v in row.items()
            }
            identity = tuple(values[c.name] for c in mapper.primary_key)
            if kind == "demo":
                # Use natural keys and generated IDs, so PostgreSQL sequences stay in sync.
                values.pop("id")
                if "device_id" in values:
                    values["device_id"] = session.scalar(
                        sa.select(MODELS["device"].id).where(MODELS["device"].code == "PCS_001")
                    )
                if "channel_id" in values:
                    values["channel_id"] = session.scalar(
                        sa.select(MODELS["channel"].id).where(
                            MODELS["channel"].code == "CH_MODBUS_001"
                        )
                    )
                existing = session.scalar(sa.select(model).where(model.code == values["code"]))
            else:
                existing = session.get(model, identity)
            if existing is None:
                session.add(model(**values))
            elif updates:
                for key in updates:
                    setattr(existing, key, values[key])
        session.flush()
