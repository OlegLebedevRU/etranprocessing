# Multi-Tenant Authentication & Organization Switching Architecture

## 1. Overview & Objectives

In the **etranprocessing / MenuBuilder** ecosystem, users interact with tenant-scoped entities (terminals, menu variants, payment groups, billing subscriptions, reports).

This document specifies the multi-tenant context switching architecture that allows privileged users (superusers/administrators) to securely select and operate within the context of any organization, while strictly preventing regular users from accessing or discovering tenant-switching capabilities.

---

## 2. Token Architecture & Claims Specification

Authentication uses signed JWTs (HMAC-SHA256, `HS256`) with a shared secret key configured in both Nginx (`JWT_SECRET_HEX`) and FastAPI services.

### A. Master Token (Platform / Superuser Session)
- **Issued to:** Superusers during login.
- **Purpose:** Platform administration, querying available tenants, requesting tenant tokens.
- **Claims:**
  ```json
  {
    "sub": "o.lebedev",
    "role": "superuser",
    "is_superuser": true,
    "token_type": "master",
    "org": "0",
    "org_id": null,
    "can_switch_org": true,
    "iat": 1787000000,
    "exp": 1787028800
  }
  ```

### B. Tenant Token (Context Session)
- **Issued to:**
  1. Regular users upon login (bound to their fixed `org_id`).
  2. Superusers upon switching organization context.
- **Purpose:** All standard business operations (`/api/monitoring`, `/api/menu-variants`, `/api/billing/*`, `/api/report/*`).
- **Claims:**
  ```json
  {
    "sub": "o.lebedev",
    "org": "223",
    "org_id": 223,
    "role": "superuser",
    "is_superuser": true,
    "token_type": "tenant",
    "orig_sub": "o.lebedev",
    "is_imp": true,
    "iat": 1787000000,
    "exp": 1787028800
  }
  ```

---

## 3. Anti-Spoofing & Gateway Enforcement (Nginx)

Nginx validates JWT signatures at the ingress perimeter using `ngx-http-auth-jwt-module`.

### Anti-Spoofing Configuration:
To prevent client header injection attacks, Nginx uses `auth_jwt_extract_var_claims` (which sets internal variables) rather than `auth_jwt_extract_request_claims` (which can append to attacker-controlled headers).

```nginx
# Billing API: Terminates JWT, forwards verified claims as headers to ProcessingBackend
location /api/billing/ {
    auth_jwt_extract_var_claims sub org role;

    proxy_pass http://processing-backend:8000/api/billing/;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header Authorization "";
    proxy_set_header jwt-sub $jwt_claim_sub;
    proxy_set_header jwt-org $jwt_claim_org;
}

# MenuBuilder API: Validates JWT and forwards verified claim variables to backend
location /api/ {
    auth_jwt_extract_var_claims sub org role;

    proxy_pass http://menubuilder-backend:8000;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    proxy_set_header jwt-sub $jwt_claim_sub;
    proxy_set_header jwt-org $jwt_claim_org;
    proxy_set_header jwt-role $jwt_claim_role;
}
```

---

## 4. FastAPI Dependency Hierarchy

Access control is enforced declaratively using FastAPI dependencies:

```
                  ┌──────────────────────────────┐
                  │      get_current_user        │
                  │   (Validates JWT signature   │
                  │     & extracts payload)      │
                  └──────────────┬───────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
  ┌──────────────────────────────┐ ┌──────────────────────────────┐
  │      require_superuser       │ │    require_tenant_context    │
  │ (Rejects non-superusers with │ │  (Requires org_id > 0 and    │
  │       403 Forbidden)         │ │   token_type == "tenant")    │
  └──────────────┬───────────────┘ └──────────────────────────────┘
                 │
                 ▼
  - GET  /api/admin/tenants/available
  - POST /api/admin/tenants/switch
```

---

## 5. Modular User Store & Extensibility Architecture

To allow seamless future migration to a PostgreSQL `users` table or a dedicated external Auth Microservice, the user store is abstracted into `AbstractUserStore`:

```python
class AbstractUserStore(ABC):
    @abstractmethod
    async def get_by_username(self, username: str) -> UserRecord | None: ...

    @abstractmethod
    async def authenticate(self, username: str, plain_password: str) -> UserRecord | None: ...
```

### Implementations:
1. **`ConfigUserStore` (Active)**: Loads users from `settings.auth_users`. Identifies `o.lebedev` as a superuser.
2. **`DatabaseUserStore` (Pluggable)**: Adapter skeleton for direct queries to `users` table.
3. **`RemoteAuthUserStore` (Pluggable)**: Adapter skeleton for delegating authentication to an external auth service.

---

## 6. Frontend Architecture (MenuBuilder/frontend)

1. **State Management**:
   - `mb_token`: Active working JWT sent in `Authorization: Bearer <token>` for all API calls.
   - `mb_master_token`: Master JWT retained for superusers to enable switching between tenants.
   - `mb_is_superuser`: Boolean flag determining admin capabilities.
2. **Conditional Rendering (`OrgSwitcher`)**:
   - Strictly renders `null` if the user is not a superuser.
   - For superusers: renders an Ant Design `Select` dropdown populated from `GET /api/admin/tenants/available` with a "Войти" button.
3. **Context Switching**:
   - Superuser clicks "Войти" -> `POST /api/admin/tenants/switch { org_id: X }`.
   - Backend returns new tenant-scoped JWT.
   - Frontend sets `localStorage.setItem("mb_token", access_token)` and refreshes page/views.
