"""Explicit ORM seeds with the same behavior on all supported databases."""

import json
import sqlalchemy as sa
from sqlalchemy.orm import Session
from .models import Base, Device
from .seed_data import CATALOG, DEMO

MODELS = {mapper.local_table.name: mapper.class_ for mapper in Base.registry.mappers}
CATALOG_KEYS = {
    "cfg_device_type": ("type_code", "type_version"),
    "cfg_type_point": ("type_code", "type_version", "semantic_code"),
}


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
    if kind == "demo":
        apply_seed(session, "catalog")
    session.flush()
    for name, rows, updates in CATALOG if kind == "catalog" else DEMO:
        model = MODELS[name]
        mapper = sa.inspect(model)
        for row in rows:
            values = {
                k: json.loads(v) if isinstance(mapper.columns[k].type, sa.JSON) else v
                for k, v in row.items()
            }
            if kind == "demo":
                # Demo references stable business IDs, never generated numeric primary keys.
                values.pop("id", None)
                if name == "cfg_device":
                    station = session.scalar(sa.select(Device).where(Device.node_kind == "station"))
                    values["parent_device_id"] = station.device_id
                key = {"cfg_device": "device_id", "cfg_channel": "channel_id",
                       "cfg_binding": "binding_id", "cfg_point_yc": "point_id"}[name]
                existing = session.scalar(sa.select(model).where(getattr(model, key) == values[key]))
            else:
                existing = session.scalar(
                    sa.select(model).where(
                        *(getattr(model, key) == values[key] for key in CATALOG_KEYS[name])
                    )
                )
            if existing is None:
                session.add(model(**values))
            elif updates:
                for key in updates:
                    setattr(existing, key, values[key])
        session.flush()
