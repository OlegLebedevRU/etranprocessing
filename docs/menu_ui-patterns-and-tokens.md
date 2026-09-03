# MenuBuilder UI — Design Patterns

Reference for the frontend team (and AI agents) building or extending
`MenuBuilder/frontend`. Describes the conventions actually used across
`src/routes/*.tsx` and `src/components/*.tsx` as of the "PlaterraMonitoring"
rework (Aug 2026). Keep this in sync when a pattern changes.

## Stack

- React + Vite + TypeScript, `react-router` (v7, "Outlet"-based nested routes).
- **AntD 6.6.x** (`antd: ^6.6.1`) — use its built-in components/tokens instead
  of hand-rolled CSS wherever a reasonable equivalent exists (`Segmented`,
  `Statistic`, `Tag`, `Affix`, `theme.useToken()`, etc.).
- No CSS-in-JS library — components use plain inline `style={{...}}` objects
  and AntD design tokens (`theme.useToken()`) for colors/spacing/radius so the
  UI stays consistent if the theme changes.

## Layout shell

- `routes/layout.tsx` — top-level `Layout` with a collapsible `Sider` (width
  196 / collapsed 56, `breakpoint="lg"`) + sticky `Header` (48px) + `Content`
  (16px padding). Site name **"PlaterraMonitoring"** is shown in the Sider
  logo block and must stay consistent with the `login.tsx` page and browser
  title.
- Top-level nav order (fixed, do not reorder without a product decision):
  `Мониторинг → Управление меню → Отчёты → Лицензии → Интеграции`.
- **`SectionLayout`** (`components/SectionLayout.tsx`) — reusable two-pane
  shell for a section that itself has sub-pages (used by "Управление меню" →
  Терминалы/Меню, and "Интеграции" → API-токены/...). Slim vertical `Menu` on
  the left (170px, sticky, `top: 52`), nested route content on the right via
  `<Outlet/>`. Add new sub-sections by extending `items`/`resolvePath`/
  `resolveKey`, not by duplicating the shell.
- **`PageHeader`** (`components/PageHeader.tsx`) — every route content page
  starts with `<PageHeader title=... subtitle=... extra=.../>` for a
  consistent title bar (Title level 5, secondary 12px subtitle, right-aligned
  `extra` actions). Always use this instead of ad-hoc `<h2>`/`<div>` headers.

## Tables

- Row/cell font sizes are intentionally small (11–12px body, 10px for
  monospace/code-like values) to fit dense operational data — this is a
  deliberate "dashboard" density choice, not an oversight.
- Column ordering follows information priority: identifying column(s) first
  (ID/device_id), then the most actionable/primary data, secondary/reference
  data (SN, timestamps) later or last. When a user asks to move a column
  "right after ID", treat ID as the anchor and move both the data column and
  its directly related actions column together (e.g. Терминалы: `ID → Меню →
  actions → SN → active-dot`).
- **Do not use `width: 1` to "shrink to content"** on a table column — under
  AntD's table layout this makes the browser wrap text into narrow
  multi-character lines instead of sizing to content. Instead: omit `width`
  (let it size naturally) and put `whiteSpace: "nowrap"` on the rendered
  `Text`/`span` inside the cell. Combine with a `Tooltip` + truncation
  (`v.slice(0, N) + "…"`) for long values like serial numbers.
- Status/attention indicators use small `Tag color="..."` chips
  (`red`/`orange`/`green`/`blue`) or a colored dot (`●`) span rather than
  icons, e.g. billing status tags in `billing.tsx`
  (`billingStatusColor(status)`), the active/inactive dot in
  `monitoring.tsx`/`terminals.tsx`.
- Use AntD `Tooltip` for full values behind truncated/compact cells (SN,
  serials) — wrap the tooltip title in `Typography.Text copyable` so users can
  one-click copy the untruncated value.

## Forms & controls

- Prefer **`Segmented`** over `Radio.Group`/button groups for a small set of
  mutually exclusive options with a visual "selected" state (e.g. the
  advance-payment period picker in `billing.tsx`). Label segments in
  human units the user actually thinks in (months: "3 мес", "6 мес" — derived
  from the org's own billing period, not raw multipliers like "+1"/"+2").
- Checkboxes embedded in a table column (e.g. license/certificate line
  selection in the billing cart) should reflect derived/auto-computed state
  via a `useEffect` keyed on the inputs that drive the derivation, and must
  remain individually togglable by the user afterwards — don't force-recompute
  over a user's manual uncheck on every render, only when the driving inputs
  actually change.
- Money is **always** integer minor units (kopecks) end-to-end; format for
  display by dividing by 100 and truncating (not rounding) to whole currency
  units per product requirement ("не показывать копейки") — use
  `Math.floor(amountMinor / 100)`, never `.toFixed(2)`.

## Summary / call-to-action panels

- A prominent "amount due + action button" panel (e.g. "Сумма к оплате" in
  `billing.tsx`) should sit visually separate from the table (card-like
  container, `token.colorBgContainer` background, `token.colorBorderSecondary`
  border, `token.borderRadiusLG` radius) and stay reachable without scrolling
  back up — consider AntD `Affix` for long tables.
- Use `Tag` counters near the page header for at-a-glance breakdowns (e.g.
  "Просрочено: N" red / "Активных: N" green / "Отключение: N" blue) instead of
  a separate stats block, to keep the header compact.

## Color conventions

- Don't hardcode hex colors for status semantics if an AntD token exists;
  when a bespoke threshold color is needed (e.g. monitoring "minutes since
  last payment" indicator: yellow >60 min, red >4 h), define it as a small
  pure helper function next to the column definition (mirrors
  `stateBg`/`billingStatusColor`) rather than inlining conditionals in JSX.

## Related helpers worth reusing

- `daysUntilLicenseExpiry`, `isLicenseLapsed`, `isLicenseUrgent`,
  `shouldAutoSelectLicense` (`routes/billing.tsx`) — canonical place for any
  new "how urgent/expired is this terminal's license" logic. Keep the
  "urgent" threshold (`LICENSE_DUE_SOON_DAYS`, currently 30 days) aligned with
  backend's `settings.billing_due_soon_days` / `settings.cert_expiring_soon_days`
  (see `ProcessingBackend/backend/app/config.py`) so the UI and the backend
  agree on what "expiring soon" means.
