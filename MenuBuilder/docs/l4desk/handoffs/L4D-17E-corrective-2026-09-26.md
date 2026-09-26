# L4D-17E corrective: owner lease, durable sessions, commercial events

```yaml
status: IN_PROGRESS
handoff_status: NOT_ACCEPTED
scope: MenuBuilder backend + IoT app1 role/console boundary
environment: isolated 17E tenant 3; production MenuBuilder backend at 85965df
```

## Task intake

- Владелец: MenuBuilder — tenant, локальные сессии, usage и ledger; app1 — lease,
  диагностика WS и авторитетные `device_online`; ProcessingBackend — владелец
  миграций, но схема в этом corrective не меняется.
- Producer → transport → consumer: Browser → MenuBuilder BFF → REST app1 lease;
  app1 → REST event feed → MenuBuilder inbox → monthly charge; MenuBuilder
  start/stop → общая БД → `FinUsageDaily`.
- Инварианты: роль 5 только свой tenant; чужой owner/lease не закрывается;
  один активный session на terminal; billing только по измеренному интервалу
  и подтверждённому online; повтор stop/online без двойного начисления.
- Проверка: полные локальные suites, изолированный E2E owner/API/DB и затем
  тот же Git archive в production MenuBuilder.

## Контракт producer → consumer

| Направление | Канал / schema | Idempotency | TTL / timeout | Ошибка / совместимость |
|---|---|---|---|---|
| BFF → app1 lease | REST `/api/internal/v1/remote-input/devices/{sn}/lease`, `X-Org-Id`, `X-Role`, `X-User-Id`, `X-Session-Id`; role `l4desk_owner`, scope view/stream/input/console | lease registry по SN/browser session | lease TTL | Старый app1 даёт 403 для роли 5; нужен порядок релиза app1 → BFF. Чужой tenant 403, занято 409. |
| Browser → app1 diagnostics | WS `/api/internal/v1/diagnostics/ws/devices/{sn}`, JWT headers от nginx, явный console lease для роли 5 | lease_id + owner/session check | lease TTL | Неявная console lease остаётся только superuser; чужой/истёкший lease 4409. |
| BFF → shared DB | `l4desk_remote_sessions`, FK `terminal.id`; local session active→closed | row lock, closed state; событие `local-session-closed-{id}` | длительность UTC, секунды | Commit create/close; при ошибке metering транзакция закрытия откатывается и stop можно повторить. |
| app1 → MenuBuilder finance | event feed v1 `device_online` → durable inbox → `FinTerminalMonthlyCharge` | inbox `event_id`, unique terminal/cycle, pending retry | event `occurred_at` UTC | Shadow mode и пустой tenant allowlist не меняют финансы. SN, tenant и device_id сверяются с локальным terminal. |

MQTT топики, QoS и retain в corrective не менялись. Разрешение роли 5 для
console подтверждено пользователем в этой сессии; нормативные документы
`docs/ingress_iot/remote-input-protocol.md`,
`docs/etran_arch-remote-input-control.md` и
`docs/ops_run-remote-console-diagnostics.md` обновлены.

При изолированном API smoke owner stream acquire вернул 201, но release вернул
403, когда тестовый клиент подставил собственный `session_id`, отличный от JWT.
Это выявило разрешённый BFF override: теперь несовпадающий client `session_id`
отклоняется до вызова app1. Повторный E2E дал 400 для mismatch, 201/204 для
stream и console; stream→console scope upgrade также прошёл под owner.

IoT producer исторически записал четыре настоящих `device_online` для тестового
SN с пустыми tenant/device полями. Причина — проверка SQLAlchemy `Row` только
как `tuple/list` в resolver. Исправлено в IoT `5e1093b`; read-only resolve в
развёрнутом app1 возвращает `(tenant_id=3, device_id=1000003)`. После Agent
reconnect настоящее событие cursor 472 содержит оба идентификатора.

## Локальная валидация

- MenuBuilder: `ruff check --fix app`, `ruff format app`, `pyright app` — pass;
  `uv run pytest -q` — 487 passed, 50 warnings.
- IoT app1: targeted Ruff after removing pre-existing unused imports — pass;
  `uv run pytest -q` — 427 passed, 4 warnings. Existing full-file formatter
  drift in IoT event service remains outside this corrective.
- Pre-commit scan добавленных строк на assignment секретов — 0 кандидатов;
  `git diff --check` — pass.

## Server E2E выполнено

- App1 `5e1093b` развёрнут через Git fast-forward и Compose build/up только
  `app1`; startup и REST работают. Предыдущий образ сохранён как rollback.
- Изолированный MenuBuilder из архива Git `85965df` проверен по всем 90
  Python-файлам. Production MenuBuilder собран из того же архива; 90/90 hash
  совпадают, schema revision `027` и все 23 таблицы проходят startup check,
  `/docs` через nginx возвращает 200. Предыдущий образ сохранён как rollback.
- Owner tenant 3 получил/освободил stream и console lease, scope upgrade
  stream→console; чужой browser session ID дал 400 до IoT вызова. Owner
  test tenant получил 403 на `control/status` устройства 773 другого tenant,
  но 200 на своё устройство 1000003.
- На устройстве 1000003 owner запустил и остановил настоящий desktop stream.
  `l4desk_remote_sessions.id=422` закрыта с 5 секундами, `fin_usage_daily.id=1`
  содержит 5 video seconds и 0 posted kopecks, lease освобождена 204.
- Изолированный consumer с allowlist `[3]` и shadow=false обработал feed до
  cursor 472 за 5 страниц без quarantine. Событие `device_online` отмечено
  `finance_applied`; `fin_terminal_monthly_charges.id=1` создано один раз для
  бесплатного terminal 3718/cycle 1: 0 calculated/posted kopecks, без ledger
  transaction. Повторная подача того же event_id дала duplicate=1,
  processed=0, finance_applied_again=0. В tenant 3 ledger sum debit/credit —
  1000/1000 kopecks (существующие тестовые проводки); в monthly таблице только
  tenant 3, один charge. Production consumer flag не включался.

## Непроверенное до 17E acceptance

- Browser owner console diagnostics WS и moving frames после production deploy;
  повтор stop и отсутствие двойного usage.
- Полная матрица 17E (DST, grace, archive, reconciliation и другие шаги) и
  17F ещё не выполнены; gate не открыт.

## Cleanup

После E2E освободить test lease, вернуть consumer в disabled/shadow,
разобрать осиротевшую IoT запись `10000006` отдельным адресным действием.
Тестовую финансовую историю не удалять вручную.
