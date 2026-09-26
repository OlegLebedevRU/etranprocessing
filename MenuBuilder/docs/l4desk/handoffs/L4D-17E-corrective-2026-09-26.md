# L4D-17E corrective: owner lease, durable sessions, commercial events

```yaml
status: IN_PROGRESS
handoff_status: NOT_ACCEPTED
scope: MenuBuilder backend + IoT app1 role/console boundary
environment: isolated 17E tenant 3; production MenuBuilder image preserved
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
- Проверка: полные локальные suites и изолированный E2E owner/browser/DB;
  текущий production MenuBuilder не пересоздавать до проверки кандидата.

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
отклоняется до вызова app1. Повторный E2E release выполняется после нового
кандидата; тестовая аренда истекла, status показывает `active=false`.

## Локальная валидация

- MenuBuilder: `ruff check --fix app`, `ruff format app`, `pyright app` — pass;
  `uv run pytest -q --maxfail=3` — 486 passed, 50 warnings.
- IoT app1: `ruff check` на изменённых файлах — pass;
  `uv run pytest -q --maxfail=3` — 426 passed, 4 warnings.
- Pre-commit scan добавленных строк на assignment секретов — 0 кандидатов;
  `git diff --check` — pass.

## Непроверенное до server E2E

- Owner JWT stream/console после деплоя app1; tenant 3, device 1000003.
- Одна сохранённая закрытая сессия и `FinUsageDaily` после browser stop;
  секунды, повтор stop и отсутствие двойного usage.
- `device_online` → не более одного monthly charge в cycle при повторе;
  debit=credit и отсутствие начислений другим tenant.
- Byte parity кандидата с Git, rollback images и отсутствие регрессий
  рабочего MenuBuilder при изолированном тесте.
- Полная матрица 17E (DST, grace, archive, reconciliation и другие шаги) и
  17F ещё не выполнены; gate не открыт.

## Cleanup

После E2E освободить test lease, вернуть consumer в disabled/shadow,
разобрать осиротевшую IoT запись `10000006` отдельным адресным действием.
Тестовую финансовую историю не удалять вручную.
