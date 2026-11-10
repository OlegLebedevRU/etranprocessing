# Payment report filters, parameters and encoding

## Task intake

- Scope: MenuBuilder payment report/UI/CSV and the ProcessingBackend payment
  request decoder, which owns persisted payment parameters. No shared model or
  schema migration. Isolated branch fix/payment-report-details, base26f4294.
- Applied repo-intake-and-routing and api-and-data-ownership. Flow: terminal form
  → PB parser/service → payments/payment_params → MB tenant-scoped reader → UI/CSV.
- Preserve report/export permissions, tenant scope, dates/timezones, top limit,
  transaction IDs, monetary values and payment creation/update semantics.
- Authorized production read only: org475, transaction0262_101126_15124497.
  No production payment writes, historical backfill or deployment in this task.

## Diagnosis

- Readiness via SSH2026-11-10T14:30UTC: RAM1742MiB available, disk52%, load1.45.
  MCP Ops unavailable in this session.
- Four stored parameters. Values for codes2 and3 already contain U+FFFD
  replacement characters; no percent-encoded bytes remain. Personal values were
  not printed or copied into fixtures. Descriptions are valid Cyrillic.
- PB decoded the form body as Windows-1251 but then unquote_plus defaulted to
  UTF-8/replacement for percent bytes. It also decoded escaped ampersands before
  splitting fields. parse_params_string preferred any equals sign over an earlier
  space and could drop a value containing equals.
- Original letters cannot be reconstructed from U+FFFD. This task fixes future
  intake, without guessing historical customer data.

## Changes

- Optional payment_type=all|card|cash. Default all includes other types; cash
  includes0/1 consistently with existing cash-ledger calculations, card is2.
  Both total and row queries retain the same tenant and type predicates.
- UI filter defaults to all. CSV retains the existing displayed result/top limit
  and adds unioned parameter columns keyed by code and description. Missing values
  stay empty; repeats are joined rather than silently overwritten. UTF-8 BOM and
  CSV quoting retained, spreadsheet formula-like text is escaped.
- PB splits raw form/query delimiters before percent decoding; supports UTF-8,
  legacy Windows-1251 and explicit Windows-1251 charset, literal plus/equals/&.
  Earliest parameter separator is used so equals inside values is retained.

## Validation

- MenuBuilder backend full suite1209passed/25skipped; ProcessingBackend full
  suite363passed/2skipped. Ruff checks/format and Pyright app passed in both.
- Frontend161unit tests and production build passed. Browser scenario exercises
  actual filter selection/API requests and downloaded CSV:1passed in15.3s.
- Initial parser regression exposed value-with-equals loss and was fixed before
  full PB suite. Browser fixture iterations corrected virtual-select interaction
  and icon-prefixed accessible button name; these are not production failures.
- No live payment was created or altered. No raw request/recovery source exists
  in the inspected payment record; original terminal/provider journal would be
  needed for a separately reviewed historical repair.

## Release and compatibility

- Release PB and MB backend before frontend. Old MB silently ignores an unknown
  filter, so frontend must not be shipped ahead of its API. Roll back frontend
  before removing backend filter support. No migration/backfill required.
- User's existing root changes are untouched; all implementation is in the
  isolated checkout. Test fixtures are synthetic and contain no customer values.
