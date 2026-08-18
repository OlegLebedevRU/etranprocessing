# Billing Architecture — Flow &amp; Math Reference

Authoritative reference for the terminal license/certificate billing system.
Covers both backend (`ProcessingBackend/backend/app/services/billing.py`,
`app/routers/billing.py`, `app/services/cert_billing.py`) and the MenuBuilder
frontend cart page (`MenuBuilder/frontend/src/routes/billing.tsx`,
`components/CheckoutModal.tsx`). Supersedes the historical planning docs in
`docs/billing-implementation-plan.md` and the pre-fix audit in
`docs/billing-implementation-review/REVIEW.md` (both kept for history — see
the status notes added at the top of each).

## 1. Domain model

Each **terminal** has, independently:

- A **license** (`License` row, `expires_at`, `renewal_enabled`,
  `deactivation_requested_at`, optional `monthly_price_override_minor`).
- A **certificate** (`cert_serial`, `cert_not_valid_after`, plus a
  `CertificatePin` PIN-issue workflow — see
  `ProcessingBackend/docs/certificates-flow.md`).

Both are billed **independently** and can be paid for in the same checkout
(`CheckoutRequest.items[].include_license` / `include_cert_pin`), but the math
for each line is entirely separate (a license amount and a cert-PIN amount
never share a period or a due-date calculation).

All money is **integer minor units** (kopecks) everywhere in the backend and
in transit over the API — never a float. The frontend cart truncates (not
rounds) to whole currency units purely for *display* (`Math.floor(minor/100)`)
per product requirement; the checkout payload still sends exact minor units.

## 2. License billing status state machine

`resolve_billing_status()` (`app/services/billing.py`) computes one of:

| Status | Condition |
|---|---|
| `ADMIN_DISABLED` | `Terminal.is_active == False` (operator-level kill switch, unrelated to billing) |
| `NO_LICENSE` | Terminal has no `License` row at all |
| `DISABLED` | `renewal_enabled == False` and license already expired (user deactivated and the paid term has run out) |
| `DEACTIVATION_SCHEDULED` | `renewal_enabled == False` but license `expires_at` is still in the future — user requested deactivation but is still inside the period they already paid for |
| `OVERDUE` | `renewal_enabled == True` and `expires_at <= as_of` |
| `DUE_SOON` | `renewal_enabled == True`, not expired yet, but `expires_at <= as_of + due_soon_days` (config: `settings.billing_due_soon_days`, default 30) |
| `ACTIVE` | Everything else |

Key invariant: **`Terminal.is_active` is never touched by user-initiated
billing actions** — it's an admin-only switch. A user "deactivating" a
terminal only sets `License.renewal_enabled = False`; the terminal keeps
working (and stays billable/OVERDUE-free) until its already-paid period
actually expires (`DEACTIVATION_SCHEDULED → DISABLED`).

## 3. "Lapsed" vs "due soon" — why the distinction matters

`is_license_lapsed(expires_at, as_of)` → `True` iff the license is missing or
`expires_at <= as_of`. This is a **stricter** condition than `OVERDUE` status
alone would suggest when read casually — it's the single source of truth used
at checkout time to decide how many periods must be paid **right now**:

```python
lapsed = is_license_lapsed(billing.license_expires_at, as_of)
periods_due = 1 if lapsed else 0
total_periods = periods_due + item.advance_periods
if total_periods == 0:
    raise HTTPException(400, "No periods to pay for terminal {id}")
```

**Architectural consequence (important for any future UI work):** a license
that is `DUE_SOON` (expiring within 30 days but not yet expired) is **not**
lapsed, so `periods_due = 0` for it. If the global cart is set to
`advance_periods = 0` ("Только задолженность" / "debt only"), a due-soon
terminal contributes `total_periods = 0` and the backend **rejects the whole
checkout with HTTP 400** the moment that terminal's `include_license` line is
included. There is currently **no per-line `advance_periods` override** — the
frontend sends one global `advance_periods` for every line in the cart
(`CheckoutModal.tsx`). This is why the MenuBuilder cart page
(`routes/billing.tsx`) must:

1. Auto-select (check) any license expiring within `LICENSE_DUE_SOON_DAYS`
   (30 days, mirrors backend's `billing_due_soon_days`) even if not lapsed.
2. Hide the "Только задолженность" advance-period option whenever such an
   urgent-but-not-lapsed terminal exists in the org (`hasUrgentDueSoon` in
   `billing.tsx`), forcing `advance_periods >= 1` as the default — otherwise
   the auto-checked due-soon line would be un-payable and the whole checkout
   would 400.

If a genuine "pay debt only, ignore due-soon terminals" UX is ever needed, the
correct fix is a **per-line `advance_periods`** in `CheckoutRequest` /
`CheckoutModal`, not a client-side workaround.

## 4. Period price &amp; date math

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

`advance_periods` is capped to **0, 1, or 2** at the API layer
(`item.advance_periods` in `/api/billing/checkout`; a separate
`/api/billing/reactivate` style endpoint requires `1` or `2`, never `0`).

## 5. Certificate PIN pricing

Independent of the license line. `resolve_effective_price()` /
`resolve_cert_policy()` (`app/services/cert_billing.py`) determine:

- `cert_operation`: `primary_issue` (no `cert_serial` yet) vs `reissue`
  (renewing an existing cert).
- `cert_pin_price_minor`: 0 (free, e.g. within a grace policy) or a positive
  amount. **A `cert_price <= 0` line cannot be checked out** — the API
  rejects it with "Certificate PIN ... is free — request it directly instead
  of paying for it" (400). Free-PIN issuance is a separate, un-billed flow
  (`generate_pin` in the MCP pin-server, or a dedicated free-issue endpoint),
  not part of `/api/billing/checkout`.
- `cert_expiring_soon` (`is_cert_expiring_soon`) uses the same "≤ N days"
  pattern as license `due_soon`, config `settings.cert_expiring_soon_days`
  (default 30) — kept deliberately symmetric with the license threshold so
  "expiring soon" means the same thing for both artifact types in the UI.
- Tenant self-service PIN purchase requires
  `org_settings.tenant_pin_creation_enabled`; otherwise 403.

## 6. Checkout flow end-to-end

1. `POST /api/billing/checkout` with `items: [{terminal_id, include_license,
   include_cert_pin, advance_periods}]`.
2. Per item: validate org ownership, `advance_periods` range, "nothing
   selected" (400 if neither flag set), `ADMIN_DISABLED` terminals rejected.
3. License line (if `include_license`): compute `lapsed`, `periods_due`,
   `total_periods`, `amount_minor = total_periods * period_price`, projected
   `new_expires_at` (mode = reactivation/renewal per §4).
4. Cert line (if `include_cert_pin`): validate tenant flag + non-zero price,
   compute `amount_minor = cert_pin_price_minor`, snapshot the cert policy
   (`build_cert_policy_snapshot`) onto the order item for audit/reproducibility.
5. **Idempotency guard**: reject (409) if a `pending` `BillingOrder` already
   references any of the same terminals — prevents duplicate concurrent
   checkouts for the same terminal while a payment is in flight.
6. Persist `BillingOrder` (+ `BillingOrderItem` rows) with `status="pending"`,
   call the configured `payment_provider.create_checkout(...)` for a redirect
   URL, commit, return `{order_id, amount_minor, payment_url, items[]}`.
7. On payment confirmation (webhook/provider callback, outside this doc's
   scope — see `app/services/payment_provider.py`), the order is marked paid
   and `License.expires_at` / cert issuance are applied.

## 7. Forecast &amp; org summary

`build_org_summary_data()` aggregates per-terminal `TerminalBillingResult`
into: overdue amount/count, active count, deactivation-scheduled count,
admin-disabled count, nearest required payment date, and a monthly
`forecast[]` (only terminals with `included_in_forecast == True`, i.e.
`renewal_enabled` and not `OVERDUE`/`NO_LICENSE`, are projected forward).

## 8. MenuBuilder ↔ ProcessingBackend integration

- MenuBuilder's nginx proxies `/api/billing/*` straight through to
  `processing-backend` (same Docker network); ProcessingBackend independently
  validates the JWT (`python-jose`, same `JWT_SECRET_HEX` as MenuBuilder).
- **`org_id` must always reach the backend as an `int`.** The JWT claim is a
  string in the token (`"org": "1"`); both MenuBuilder's own report endpoints
  and ProcessingBackend's billing endpoints filter SQL by integer `org_id`
  columns, so any hop that forwards the raw string claim without casting will
  produce `asyncpg.exceptions.DataError` (see incident in §9).

## 9. Known-fixed pitfalls (do not reintroduce)

- **`org_id` as string vs int** — MenuBuilder's `get_current_user()`
  (`MenuBuilder/backend/app/auth.py`) now casts the JWT `org`/`orgId`/`org_id`
  claim to `int` (falling back to `None` on parse failure) before it's used
  anywhere downstream. This fixed 500s on `/api/reports/payments`,
  `/api/reports/balance-by-terminal`, `/api/reports/balance-by-tsp`, all of
  which bind `org_id` into raw SQL. `terminal_bindings.py` already had a local
  `int(org_id)` workaround for the same root cause — that workaround is now
  redundant but harmless.
- **MCP pin-server `generate_pin` missing `org_id` on INSERT** — the
  `certificate_pins` table requires `org_id NOT NULL`; the terminal resolved
  via `_resolve_terminal` already carries `org_id`, it just wasn't threaded
  through to the `INSERT` statement. Fixed in an earlier session — if you
  touch `mcp-pin-server/src/pin_server/server.py`'s `generate_pin`, keep
  `org_id` in the `INSERT ... VALUES (...)`.
- **"No periods to pay" 400 for due-soon-but-not-lapsed terminals** — see §3.
  Any future auto-select feature must account for this constraint before
  defaulting a due-soon (non-lapsed) terminal into a cart with
  `advance_periods = 0`.
