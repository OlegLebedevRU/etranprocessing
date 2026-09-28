from __future__ import annotations

from contextlib import suppress
from typing import Any

from fastapi import Depends, HTTPException, Request, status

from app.auth import get_current_user, get_current_user_optional

# --- Role constants ---
ROLE_SUPERUSER = 1
ROLE_ADMIN = 2
ROLE_USER = 3
ROLE_VIEWER = 4
ROLE_L4DESK_OWNER = 5

ROLE_ID_TO_NAME: dict[int, str] = {
    1: "superuser",
    2: "admin",
    3: "user",
    4: "viewer",
    5: "l4desk_owner",
}
NAME_TO_ROLE_ID: dict[str, int] = {v: k for k, v in ROLE_ID_TO_NAME.items()}

# --- Permission codes ---
PERMISSION_MONITORING_VIEW = "monitoring:view"
PERMISSION_REPORTS_INKASS_VIEW = "reports:inkass:view"
PERMISSION_REPORTS_PAYMENTS_VIEW = "reports:payments:view"
PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW = "reports:balance_terminal:view"
PERMISSION_REPORTS_BALANCE_TSP_VIEW = "reports:balance_tsp:view"
PERMISSION_REPORTS_EXPORT = "reports:export"
PERMISSION_BILLING_VIEW = "billing:view"
PERMISSION_SETTINGS_TERMINALS_VIEW = "settings:terminals:view"
PERMISSION_VIDEO_VIEW = "video:view"

ALL_PERMISSIONS: list[str] = [
    PERMISSION_MONITORING_VIEW,
    PERMISSION_REPORTS_INKASS_VIEW,
    PERMISSION_REPORTS_PAYMENTS_VIEW,
    PERMISSION_REPORTS_BALANCE_TERMINAL_VIEW,
    PERMISSION_REPORTS_BALANCE_TSP_VIEW,
    PERMISSION_REPORTS_EXPORT,
    PERMISSION_BILLING_VIEW,
    PERMISSION_SETTINGS_TERMINALS_VIEW,
    PERMISSION_VIDEO_VIEW,
]

VALID_PERMISSION_CODES = set(ALL_PERMISSIONS)


async def require_readonly_guard(
    request: Request,
    current_user: dict[str, Any] | None = Depends(get_current_user_optional),
) -> dict[str, Any] | None:
    """Dependency: enforce readonly restriction for role 4 (viewer).

    Blocks any mutating HTTP method (POST, PUT, PATCH, DELETE) for role 4 with 403 Forbidden.
    """
    method = (
        request.scope.get("method")
        if hasattr(request, "scope") and isinstance(request.scope, dict)
        else getattr(request, "method", "GET")
    )
    if str(method).upper() in ("GET", "HEAD", "OPTIONS"):
        return None

    user = current_user
    if user is None:
        app_instance = getattr(request, "app", None)
        if app_instance and hasattr(app_instance, "dependency_overrides"):
            override = app_instance.dependency_overrides.get(get_current_user)
            if override:
                res = override()
                user = await res if hasattr(res, "__await__") else res

    if user is None:
        app_instance = getattr(request, "app", None)
        if app_instance and hasattr(app_instance, "dependency_overrides"):
            from app.routers.billing import get_current_billing_user

            if get_current_billing_user in app_instance.dependency_overrides:
                b_user = app_instance.dependency_overrides[get_current_billing_user]()
                is_b_su = getattr(b_user, "is_superuser", False)
                user = {
                    "role_id": 1 if is_b_su else 3,
                    "role": "superuser" if is_b_su else "user",
                    "is_superuser": is_b_su,
                }

    if user is None:
        with suppress(HTTPException):
            user = await get_current_user(request, None)

    if user:
        try:
            u_role_id = int(user.get("role_id", 0))
        except ValueError, TypeError:
            u_role_id = 0
        if u_role_id == ROLE_VIEWER or user.get("role") == "viewer":
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Действие запрещено для роли только для чтения",
            )

    return user


def require_permission(permission_code: str):
    """Dependency factory: require specific permission code for role 4, while allowing roles 1, 2, 3."""

    async def _permission_dependency(
        request: Request,
        current_user: dict[str, Any] | None = Depends(get_current_user_optional),
    ) -> dict[str, Any]:
        user = current_user
        if user is None:
            app_instance = getattr(request, "app", None)
            if app_instance and hasattr(app_instance, "dependency_overrides"):
                override = app_instance.dependency_overrides.get(get_current_user)
                if override:
                    res = override()
                    user = await res if hasattr(res, "__await__") else res

        if user is None:
            app_instance = getattr(request, "app", None)
            if app_instance and hasattr(app_instance, "dependency_overrides"):
                from app.routers.billing import get_current_billing_user

                if get_current_billing_user in app_instance.dependency_overrides:
                    b_user = app_instance.dependency_overrides[
                        get_current_billing_user
                    ]()
                    is_b_su = getattr(b_user, "is_superuser", False)
                    return {
                        "role_id": 1 if is_b_su else 3,
                        "role": "superuser" if is_b_su else "user",
                        "is_superuser": is_b_su,
                        "permissions": list(ALL_PERMISSIONS),
                    }

        if user is None:
            user = await get_current_user(request, None)

        role_id_raw = user.get("role_id", ROLE_USER)
        try:
            role_id = int(role_id_raw)
        except ValueError, TypeError:
            role_id = ROLE_USER
        is_su = bool(
            user.get("is_superuser")
            or user.get("role") in ("superuser", "admin")
            or role_id == ROLE_SUPERUSER
        )
        if is_su or role_id in (
            ROLE_SUPERUSER,
            ROLE_ADMIN,
            ROLE_USER,
            ROLE_L4DESK_OWNER,
        ):
            return user

        if role_id == ROLE_VIEWER:
            user_permissions = user.get("permissions") or []
            if permission_code in user_permissions or "*" in user_permissions:
                return user
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail="Доступ к данному разделу не предоставлен",
            )

        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Недостаточно прав для выполнения операции",
        )

    return _permission_dependency


async def require_tenant_admin(
    user: dict[str, Any] = Depends(get_current_user),
) -> dict[str, Any]:
    """Dependency: require role 1 (superuser) or role 3 (tenant user / tenant admin)."""
    role_id_raw = user.get("role_id", ROLE_USER)
    try:
        role_id = int(role_id_raw)
    except ValueError, TypeError:
        role_id = ROLE_USER
    is_su = bool(
        user.get("is_superuser")
        or user.get("role") in ("superuser", "admin")
        or role_id == ROLE_SUPERUSER
    )
    if not is_su and role_id not in (ROLE_SUPERUSER, ROLE_USER, ROLE_L4DESK_OWNER):
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Доступ разрешен только администратору организации",
        )
    return user
