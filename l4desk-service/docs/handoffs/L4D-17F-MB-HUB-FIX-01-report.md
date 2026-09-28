# L4D-17F-MB-HUB-FIX-01 — Hub route и финансовый обзор

Статус: `DEPLOYED_SMOKE_PASS`; 17F black-box acceptance остаётся
`BLOCKED_TESTS` по независимым критериям. Scope изменения: MenuBuilder
backend и основной nginx route. Время browser smoke — 2026-09-27
14:41–14:46 UTC.

## Task intake

- Владелец API и данных: MenuBuilder. Browser Hub — consumer
  `/api/v1/admin/hub/*`; nginx на порту 3000 — transport; app1 владеет
  остальным общим `/api/v1/` и не менялся.
- Инварианты: JWT проверяется до Hub, роль 5 не получает admin data;
  финансовые суммы конвертируются из целых копеек без смешения `Decimal`
  с `float`; баланс, ledger и коммерческие флаги не корректируются деплоем.
- Критерий: public Hub 200 для test superuser, 403 для владельца tenant,
  finance overview без 500, обычный video start/stop после обновления.

## Изменение и доставка

- Source commit `fb2273c0bb633426067b2a9487538d84155ef446` запушен в
  `release/l4tools-1.8.2-beta-1`. В `HubService` обе суммы делятся на 100
  через `Decimal`. В `nginx-configs/port_3000.conf` добавлен конкретный
  `/api/v1/admin/hub/` route к MenuBuilder с JWT и очищенными trusted
  identity headers. Frontend и app1 не менялись.
- Git archive SHA-256:
  `1dfc2173246e140bb05e47c91940d8aaae388117cd8c57dbe963e3aa095be75c`.
  Он установлен в `/home/user1/.l4d-releases/fb2273c`; server archive
  SHA-256 совпал. Новый image ID:
  `sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090`.
  Только `menubuilder-backend` пересоздан; `nginx-default` прошёл
  `nginx -t` и был перезагружен.
- Текущий nginx-файл совпал с release copy по SHA-256
  `0b1a6f61896941d511a188f574559acf9f8fb4966e827473e9a194d6d90ef418`.
  `hub_service.py` внутри image совпал с release copy по SHA-256
  `d22a96b378eb37e46d959f6f72395c39492bf34cdcd840d28d0ed8d3a9ead009`.
  Прежний nginx-файл сохранён как `port_3000.conf.bak-fb2273c`, image
  `sha256:c70a11d88eef2697898d936278d4f8ce8066bc8cd0fa951d7ece9efda9b49e8e`
  помечен `user1-menubuilder-backend:rollback-fb2273c`.

## Проверено

- `uv run pytest -q --tb=line --disable-warnings`: 521 passed. Первый
  полный прогон: 520 passed, один несвязанный `test_ws_relay_happy_path_and_release`
  сбой; адресный повтор прошёл, затем полный прогон прошёл без ошибок.
- `uv run ruff check --fix app tests`, `uv run ruff format app tests` и
  `uv run pyright app tests/test_hub_and_reconciliation.py
  tests/test_nginx_header_contract.py`: успешно, pyright 0 errors. Широкий
  `pyright app tests` показал 20 существовавших ошибок в других тестовых
  файлах, не в изменённых. `git diff --cached --check` прошёл.
- Production `/docs` 200. В работающем backend четыре флага
  `l4desk_policy_enforcement_enabled`, `l4desk_registration_enabled`,
  `yookassa_enabled`, `iot_consumer_enabled` остались `False`.
- Browser под test superuser: публичные `/api/v1/admin/hub/registrations`
  и `/api/v1/admin/hub/finance/overview` вернули 200. Hub отобразил
  3 организации, в том числе tenant 10000 с балансом 10 ₽; общий баланс
  29 ₽. Под владельцем tenant 10000 тот же registrations API вернул 403.
- После деплоя video 1000007: 1920×1080, readyState 4, player time вырос
  до 59 секунд; `stream/stop` 200, lease DELETE 204, UI вернулся в
  «Не запущена». Среди проверенных запросов вкладки HTTP 500 не было.
  Backend container `running`, restart count 0.

## Осталось для 17F

Hub route и Decimal defect закрыты. Проверка повторного reconciliation
operation ID, correlation lookup/filter semantics, grace/block/stop за
короткий период, archive manifest/restore dry-run, 120-минутная трасса и
deployment matrix остаются адресными 17F blocker. Этот отчёт не создаёт
`H-L4D-17F-DOCS-v1` и не запускает 18A.

Тестовые browser sessions закрываются после записи evidence; серверных
тестовых сессий или активной аренды 1000007 после smoke нет.
