"""Only the singleton login policy is seeded. No users, passwords or sessions are created."""

from datetime import datetime, timezone

from sqlalchemy.orm import Session

from .models import AuthLoginPolicy


def apply_seed(session: Session, kind: str) -> None:
    """Join the caller's transaction, preserving any administrator-configured policy."""
    if kind != "policy":
        raise ValueError("Web database supports only the policy seed")
    if session.get(AuthLoginPolicy, 1) is None:
        session.add(
            AuthLoginPolicy(id=1, updated_at=datetime.now(timezone.utc).replace(tzinfo=None))
        )
