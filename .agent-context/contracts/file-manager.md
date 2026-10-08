# L4FM v2: текущий контракт

Актуально 2026-10-05. [Каноническая архитектура и план](../../docs/etran_arch-file-manager-remote-windows.md).
[История реализации / runtime evidence](../tasks/active/2026-10-05-l4fm-improvements.md).

## Выпущенный результат

- PB/MB backend: b8c609e; app1: 6209faf; frontend: 09b22e8.
- Suite 1.13.1 подписана/опубликована и установлена через l4mcp на 1000007;
  L4Con 1.12.1, ready/exit0, службы работают, свежий PB hello protocol 2.
- На suite 1.13.0 live: roots, parent navigation, upload/autorefresh,
  download/hash, home/close/reopen и последующая console admission прошли.
- После 1.13.1 проверены установка/регистрация, не интерактивная сортировка/Save As:
  desktop locked. Полный fault/soak/privilege acceptance остаётся открытым.

## Владельцы и транспорт

- IoT — единственная общая lease files/console/stream/input/view. Same-owner acquire
  не обходит конфликт; revoke не освобождает слот до stop ACK или deadline +5 с.
  Redis WATCH защищает active key и lease hash; compare-delete не удаляет нового owner.
- PB — mTLS identity, readiness/tickets, manifests, state, HEAD/checksum/version,
  commit fence/receipt reconciliation. Agent HTTPS через Leo4Proxy, не app1.
- MB — пользовательская/tenant/device policy, browser-view UUID, metadata BFF.
- Browser и agent передают bytes только через S3; PB/MB/IoT не читают Body/не relay.
- Native — MQTT parent + отдельный owned Job worker; stop ACK после выхода worker.

## Совместимость и сигнализация

- Только protocol 2 и обязательные capabilities, включая fs.proxy,
  fs.mqtt_navigation, fs.write_user, fs.drives. Readiness до acquire, heartbeat <45 с.
- 7023 start/renew и 7021 transfer — существующий MQTT RPC.
- list/stop — srv/{SN}/fmc → dev/{SN}/fmr, v=2, command_id/lease_id,
  no retain. List несёт path/offset, без storage URL или file bytes.
- IoT ожидает полную страницу до 7 с через Redis; до 64 entries / 24 КиБ,
  pending до publish, SN/lease/UUID correlation, first valid response wins.
- RPC7020/7022/7023-stop отклоняются; PB kind=list/results entries удалены.
  HTTP /v1 и queue fm_result_v1 — имена инфраструктуры, не v1 compatibility.
- Mosquitto contract 3: восемь явных outbound и шесть inbound routes;
  l4setup/l4superv мигрируют старый config, сохраняют terminal config/ACL.
  Wildcard/overlap не допускаются. Presence extra_service не менялась.

## Данные и права

- Single PUT 0–64 МиБ, SHA256, versioned S3 и exact VersionId GET.
- Upload токеном именно physical console user, CREATE_NEW/no-overwrite; агент
  не создаёт повышение прав. Предпочитается его Limited token; при отсутствии
  split-token допускается его собственный Default token, включая UAC-off admin.
  Full token без проверенной Limited пары, чужие SID/session и неоднозначный
  ответ TokenLinkedToken отклоняются. Это не снимает private-path/ACL ограничения.
- Read-only SYSTEM fallback при AccessDenied только по policy; ACL/sharing/path
  ограничения не снимаются. Local fixed/removable drives разрешены policy.
- Смена user session/LUID/instance/cert — fence. Receipt защищён в fm-state.
- Unknown commit блокирует новые transfer до сверки, запись не возобновляется.
- S3 retention пока provider lifecycle; crash staging cleanup — следующий P0.
- Сортировка всего каталога до paging: folders→files/natural names, max 65 536 entries;
  ошибки/overflow — отказ. Между страницами нет snapshot directory.

## UI и эксплуатация

- Общий /files Classic/L4Desk. Обзор парка — первый узел, без auto-acquire;
  home закрывает сеанс/обновляет парк; terminal→drives слева, папки справа.
- Modal transfer/save, autorefresh после commit, close-before-switch,
  один teardown owner, stale async fencing, manual retry only.
- Save As из click; unsupported/blocked API → browser download; AbortError → cancel;
  disk write failure не переключает destination. PB received ≠ запись на диск браузера.
- Compact tree/address/status badge, bounded table, pointer folder cursor.
- [Обновление через l4mcp](../../docs/ops_run-l4tools-update-via-l4mcp.md):
  signed package → S3/proxy → hash/signature → независимая SYSTEM-задача → verify.

## Следующие проверки

P0: crash/unknown-commit, ACK/reconnect/Redis/process fault matrix, desktop-token
security acceptance и suite soak. P1: S3 уборка/metrics, большие каталоги,
диагностика прав/read mode, стоимость polling. Не расширять операции/relay/resume
под видом исправления текущего контракта. Детальный roadmap — в архитектуре.
