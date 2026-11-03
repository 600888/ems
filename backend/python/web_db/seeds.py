"""Idempotent Web login policy and default administrator initialization."""

from datetime import datetime, timezone
import hashlib
import secrets
import uuid

import sqlalchemy as sa
from sqlalchemy.orm import Session

from .models import AuthLoginPolicy, AuthUser


def _default_password_hash() -> str:
    """Use the salted PBKDF2 format accepted by the Web C++ password verifier."""
    salt = secrets.token_hex(32)
    iterations = 600000
    digest = hashlib.pbkdf2_hmac(
        "sha256", b"888888", salt.encode("ascii"), iterations, dklen=32
    ).hex()
    return f"pbkdf2_sha256${iterations}${salt}${digest}"


def apply_seed(session: Session, kind: str) -> None:
    """Join the caller's transaction; never reset an existing admin account or policy."""
    if kind not in ("policy", "admin"):
        raise ValueError("Web database supports policy/admin seeds")
    now = datetime.now(timezone.utc).replace(tzinfo=None)
    if session.get(AuthLoginPolicy, 1) is None:
        session.add(AuthLoginPolicy(id=1, updated_at=now))
    if kind == "admin":
        session.flush()
        session.scalar(sa.select(AuthLoginPolicy).where(AuthLoginPolicy.id == 1).with_for_update())
        if session.scalar(sa.select(AuthUser).where(AuthUser.username == "admin")) is None:
            session.add(
                AuthUser(
                    user_id=str(uuid.uuid4()),
                    username="admin",
                    display_name="管理员",
                    password_hash=_default_password_hash(),
                    role_code="admin",
                    status="enabled",
                    scope_mode="all",
                    must_change_password=0,
                    temporary_password_expires_at=None,
                    password_changed_at=now,
                    created_at=now,
                    updated_at=now,
                )
            )
