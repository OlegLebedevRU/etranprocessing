"""Compatibility name for historical callers; session shutdown is part of the technical core."""

from app.services.remote_session_stop import RemoteSessionStopService

FinStopOutboxService = RemoteSessionStopService
__all__ = ["FinStopOutboxService"]
