# Billing Architecture — Flow & Math Reference

Authoritative reference for the terminal license/certificate billing system.
Owner: MenuBuilder billing domain. Verified: 2026-08-31 against shared models,
`MenuBuilder/backend/app/services/billing.py`, billing routers and frontend cart.
It supersedes the historical planning and review documents, whose references to
`renewal_enabled`, `deactivation_requested_at` and `licenses.is_active` describe
the schema before Alembic migration `019` and are not an active contract.

## 1. Domain model

Each **terminal** has, independently:

- Administrative availability (`Terminal.is_active`).
- A paid **license term** (`License.expires_at`, with optional
  `monthly_price_override_minor`).
- A **certificate** (`cert_serial`, `cert_not_valid_after`, plus a
  `CertificatePin` PIN-issue workflow — see
  `docs/certificates-flow.md`).

Both are billed **independently** and can be paid for in the same checkout
(`CheckoutRequest.items[].include_license` / `include_cert_pin`), but the math
for each line is entirely separate (a license amount and a cert-PIN amount
never share a period or a due-date calculation).

All money is **integer minor units** (kopecks) everywhere in the backend and
in transit over the API — never a float. The frontend cart truncates (not
rounds) to whole currency units purely for *display* (`Math.floor(minor/100)`)
per product requirement; the checkout payload still sends exact minor units.

## 2. License billing status state machine & Deactivation Logic

`resolve_billing_status()` (`app/services/billing.py`) computes one of:

| Status | Condition | Meaning & UX Behavior |
|---|---|---|
| `DISABLED` | `Terminal.is_active == False` | Administratively disabled terminal. Excluded from payments and forecasts. |
| `NO_LICENSE` | Terminal has no `License` row | Excluded from renewals; requires initial setup. |
| `OVERDUE` | Active terminal and `expires_at <= as_of` | Paid term lapsed. Needs one period payment starting today to restore service. |
| `DUE_SOON` | Active terminal, not expired, `expires_at <= as_of + due_soon_days` | Paid term expiring soon (default ≤ 30 days). Eligible for advance renewal. |
| `ACTIVE` | Active terminal and `expires_at > as_of + due_soon_days` | Fully active and paid ahead. |

### Deactivation & Re-enabling Logic ("Отключить" / "Включить")
1. **Clicking "Отключить"** sets `Terminal.is_active = False`. The terminal
   transitions to `DISABLED`, has no payable lines or checkboxes, and is
   excluded from cart, monitoring, terminal-facing access and forecasts.
2. **Clicking "Включить"** sets `Terminal.is_active = True`. If
   `License.expires_at > now`, no payment is required; otherwise the terminal
   becomes `OVERDUE` and restoration uses the standard checkout flow.
3. Administrative state and paid term are independent facts. Billing code must
   not recreate a second enable/renewal flag on `licenses`.

## 3. Strict Isolation from Monitoring & Menu Management

Administratively disabled terminals are isolated from operational datasets:
- **Monitoring (`/api/monitoring`)** filters terminals with:
  ```sql
  WHERE t.is_active = true
    AND t.show_in_monitoring = true
  ```
- **Terminal-facing APIs** additionally enforce license presence and
  `License.expires_at > now` in `ProcessingBackend.get_current_terminal`.
- Monitoring may still show an active but overdue terminal so operators can
  diagnose it; terminal-facing payment/menu access is denied after expiry.

## 4. "Lapsed" vs "due soon" — why the distinction matters

`is_license_lapsed(expires_at, as_of)` → `True` iff the license is missing or
`expires_at <= as_of`. This is the single source of truth used
at checkout time to decide how many periods must be paid **right now**:

```python
lapsed = is_license_lapsed(billing.license_expires_at, as_of)
periods_due = 1 if lapsed else 0
total_periods = periods_due + item.advance_periods
if total_periods == 0:
    raise HTTPException(400, "No periods to pay for terminal {id}")
```

**Architectural consequence:** a license that is `DUE_SOON` (expiring within 30 days but not yet expired) is **not**
lapsed, so `periods_due = 0` for it. If the global cart is set to
`advance_periods = 0` ("Только задолженность" / "debt only"), a due-soon
terminal contributes `total_periods = 0` and the backend rejects the checkout with HTTP 400
if that terminal's `include_license` line is selected without advance periods.
The MenuBuilder cart page (`routes/billing.tsx`):
1. Auto-selects (checks) any license expiring within `LICENSE_DUE_SOON_DAYS` (30 days) for active terminals.
2. Hides the "Только задолженность" advance-period option whenever an urgent-but-not-lapsed terminal exists in the org (`hasUrgentDueSoon` in `billing.tsx`), forcing `advance_periods >= 1` as the default.

## 5. Period price & date math

```
monthly_price = license.monthly_price_override_minor or org_settings.monthly_price_minor
period_price  = monthly_price * billing_period_months          # calculate_period_price
```

Two date-projection modes, both **anchor-preserving** (never drift to the 28th
after a Jan 31 renewal — `add_months_from_anchor` uses `relativedelta`, not
naive day-by-day iteration):

- **`reactivation`** (license was lapsed): new term starts **today**
  (`as_of`), regardless of how long the license had been expired. Missed
  periods are **never** billed retroactively — the user only ever pays for
  exactly `periods_due (=1) + advance_periods` periods, always starting now.
  `new_expires_at = as_of + billing_period_months * total_periods`.
- **`renewal`** (license still active, just topping up in advance): new term
  extends from the **existing** `expires_at`, preserving its anchor day.
  `new_expires_at = expires_at + billing_period_months * advance_periods`.

`advance_periods` is capped to **0, 1, or 2** at the API layer.

## 6. Certificate PIN pricing

Independent of the license line. `resolve_effective_price()` /
`resolve_cert_policy()` (`app/services/cert_billing.py`) determine:

- `cert_operation`: `primary_issue` (no `cert_serial` yet) vs `reissue`
  (renewing an existing cert).
- `cert_pin_price_minor`: 0 (free, e.g. within a grace policy) or a positive
  amount. **A `cert_price <= 0` line cannot be checked out** — free PIN issuance
  is handled via the un-billed MCP pin-server workflow.
- `cert_expiring_soon` (`is_cert_expiring_soon`) uses the symmetric "≤ N days"
  threshold (`settings.cert_expiring_soon_days`, default 30).
- Tenant self-service PIN purchase requires
  `org_settings.tenant_pin_creation_enabled`; otherwise 403.
- Disabled terminals are excluded from certificate purchase offerings.

## 7. Checkout flow end-to-end

1. `POST /api/billing/checkout` with `items: [{terminal_id, include_license,
   include_cert_pin, advance_periods}]`.
2. Per item: validate org ownership, `advance_periods` range, "nothing
   selected" (400 if neither flag set), `ADMIN_DISABLED` / disabled terminals rejected.
3. License line (if `include_license`): compute `lapsed`, `periods_due`,
   `total_periods`, `amount_minor = total_periods * period_price`, projected
   `new_expires_at` (mode = reactivation/renewal per §5).
4. Cert line (if `include_cert_pin`): validate tenant flag + non-zero price,
   compute `amount_minor = cert_pin_price_minor`, snapshot the cert policy
   (`build_cert_policy_snapshot`) onto the order item for audit/reproducibility.
5. **Idempotency guard**: reject (409) if a `pending` `BillingOrder` already
   references any of the same terminals.
6. Persist `BillingOrder` (+ `BillingOrderItem` rows) with `status="pending"`,
   call the configured `payment_provider.create_checkout(...)` for a redirect
   URL, commit, return `{order_id, amount_minor, payment_url, items[]}`.
7. On payment confirmation (webhook/provider callback), the order is marked paid
   and `License.expires_at` / cert issuance are applied.

## 8. Forecast & org summary

`build_org_summary_data()` aggregates per-terminal `TerminalBillingResult`
into: overdue amount/count, active count, disabled count,
admin-disabled count, nearest required payment date, and a monthly
`forecast[]` (only administratively active terminals with an eligible license
and `included_in_forecast == True` are projected forward).

## 9. MenuBuilder ↔ ProcessingBackend integration

- MenuBuilder backend owns the user-facing `/api/billing/*` use cases. Its nginx
  terminates JWT and forwards canonical `X-User-Id`, `X-Org-Id` and
  `X-User-Role` headers; the backend applies tenant scope.
- **`org_id` must always reach the backend as an `int`.** The JWT claim is a
  string in the token (`"org": "1"`); both MenuBuilder's own report endpoints
  and ProcessingBackend's billing endpoints filter SQL by integer `org_id`
  columns.

## 10. Known-fixed pitfalls (do not reintroduce)

- **`org_id` as string vs int** — MenuBuilder's `get_current_user()`
  (`MenuBuilder/backend/app/auth.py`) casts the JWT `org` claim to `int`.
- **MCP pin-server `generate_pin` missing `org_id` on INSERT** — keep `org_id` in the `INSERT`.
- **"No periods to pay" 400 for due-soon-but-not-lapsed terminals** — see §4.
- **State duplication** — do not reintroduce `renewal_enabled`,
  `deactivation_requested_at` or `licenses.is_active`; use
  `Terminal.is_active + License.expires_at`.
- **Disabled terminal isolation** — `Terminal.is_active = false` terminals must
  never show payable lines or pass terminal-facing authorization.
