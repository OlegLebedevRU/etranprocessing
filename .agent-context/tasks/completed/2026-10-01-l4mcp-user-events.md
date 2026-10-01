# l4mcp:900–999, корреляция и история

## Scope и владельцы

Владелец изменения — l4mcp, база origin/main8b039a4. MenuBuilder4a730fb и
IoT2cda32f уже развёрнуты. Новый MQTT client, схему и nginx
не меняем. В ходе E2E пользователь отдельно разрешил исправление буферов
native l4con 1.9.4; x64 установлен после ручной остановки/запуска служб.
Авторизация каждого tool — актуальный API token MenuBuilder;
history/structured send ограничены current tenant даже для superuser.
Пользователь разрешил следующий этап, выпуск176→registry→pull только l4mcp
и E2E событий900–999 на локальном773 tenant1. Токен получен, в Git не хранится.

## Результат

- terminal_events_search: read-only MenuBuilder API, исходный payload,
  server ID/time, нормализованные446–448, cursor и has_more. Без lease/offset.
- console_send_event: common console executor/preflight/lease/RPC7001,
  CRT argv через фиксированный PowerShell Start-Process, UTF-8/base64.
  Oversize исключает payload,447=-2; invalid int32=-1; invalid UUID — отказ.
- console_run: explicit command, опциональный UUID в результате и session ID.
  Timeout/backend error сохраняют metadata. Release остаётся обязательным.
- Raw command cap2048 сохранён; generated cap4096 совпадает с IoT max_length
  и ограничен фиксированным шаблоном. Нового свободного executor нет.
- Classic773: read-only IoT проверка показала410 старых строк, owned0,
  последнее событие до migration. Это ожидаемое скрытие NULL history;
  SN локально совпал с IoT/current tenant1. Новые другие события получают tenant.

## Проверки

Локально Ruff/format/Pyright src прошли;21 tests passed, включая Windows CRT
roundtrip, UTF-8 границы и худший случай1024 кавычек, safe argv, tenant refusal,
history bearer-only, ошибки API, timeout и обязательное освобождение lease.
Production l4mcp: source d992da6dea7423e8a6759711c135c76c09beeb18,
digest sha256:79ed31e20ea742318ebe763cb5e131e74d22faf97005f714f57f17d44dffaeb4,
container17481e60bc24, restart0. Builder176 → registry → production87
прошёл; builder20 passed/1 Windows-only skipped, Ruff/format/Pyright прошли.
E2E773: события1356991/1356992 с одним UUID,447=7/INT32_MIN, чтение в новом
MCP-сеансе, cursor/limit1 и фильтр992 прошли. Raw cmd событие1356994 прошло.
Classic existing internal list возвращает новые события773 tenant1;
410 старых NULL-tenant строк скрыты намеренно, backfill не выполнялся.
Полный1024-byte Unicode payload выявил silent truncation в l4con1.9.3:
CLI1/PowerShell parse error, события нет. Исправление1.9.4 поддерживает4096
Unicode characters и strict rejection126, x86/x64 build и native tests прошли.
После установки1.9.4 повтор boundary E2E прошёл: IDs1357000–1357003,
полный1024-byte Unicode payload, INT32_MAX, oversize→нет446/447=-2,
invalid int32→447=-1. Все1024 кавычки сохранены (1357005).
Defaults999/447=0/без448 прошли (1357006), native invalid UUID CLI2.
Storm: два вызова в одной remote command дали0/5, одна запись1357004.
Superuser tenant1 получил отказ истории device1000011 tenant10000;
отдельного token tenant2 и runtime миграции устройства в этом прогоне не было.
Локальный неавторизованный CLI3; L4Con PID188684 не изменился после E2E.
Classic existing internal list773/org1:total12, включает все новые тестовые IDs.
Win7/POSReady runtime не проверен; сборки совместимы по штатным флагам.
Установленный EXE SHA256867e7eda78bc8305ba05024dfcd734bd44227bc789f17ff54c2bf72f62f5caa6,
version1.9.4.0, backup1.9.3 сохранён. Новый l4tools package не собирался.
MCP auth token не сохраняется в документах, логах или исходниках.

Полный контракт, сценарии и ограничения:
[план](../../../docs/menu_arch-l4mcp-user-events-plan.md).
