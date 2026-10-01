# l4mcp:900–999, корреляция и история

## Scope и владельцы

Владелец изменения — l4mcp, база origin/main8b039a4. MenuBuilder4a730fb и
IoT2cda32f уже развёрнуты. Новый MQTT client, native l4con, схему и nginx
не меняем. Авторизация каждого tool — актуальный API token MenuBuilder;
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
Runtime release/E2E выполняются после публикации; итоговое evidence дописать.
MCP auth token не сохраняется в документах, логах или исходниках.

Полный контракт, сценарии и ограничения:
[план](../../../docs/menu_arch-l4mcp-user-events-plan.md).
