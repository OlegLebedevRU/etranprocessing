# Задание агенту: файловый менеджер только по требованию

## Готовый текст продолжения

> Сверь [итоговый handoff](../completed/2026-11-12-fm-on-demand-release.md) и
> [контракт FM по требованию](../../../docs/etran_arch-file-manager-on-demand-readiness.md).
> Реализация и пилот773 завершены; не повторяй их. Для следующего rollout сначала
> получи явно названную группу терминалов. Отдельно исследуй единичный таймаут
> probe: без автоматического повторения, legacy fallback и расширения FM-нагрузки.
> Scope: ProcessingBackend, shared, MenuBuilder backend/frontend; native L4Con и
> signed packaging — в отдельном `OlegLebedevRU/l4tools`; app1 — в его репозитории после
> проверки собственного AGENTS.md. Цель: отсутствие фоновых FM HTTPS/DB-записей
> у idle агента и readiness fan-out на fleet page. Не меняй платежи, сертификатную
> выдачу, MQTT presence, общую lease или политику файловых прав. Подготовь
> совместимые изменения, проверки и pilot handoff; деплой и terminal update —
> только в явно разрешённом пользователем контуре. До изменения MQTT-клиента
> задай обязательный вопрос о типе клиента и дождись ответа; существующий L4Con
> описан как extra_service, но это не заменяет ответ пользователя.

Это задание для следующей сессии, не запуск другого агента в текущей задаче.
Названные здесь каталоги не расширяют текущий read scope автоматически.

## Обязательный intake

1. Читать AGENTS.md, repo-intake-and-routing, FM contract/card и соответствующие
   cross-stack/native/lease/verification skills. Начинать от свежих main/master.
2. Получить разрешённый source app1. Проверить producer и consumer fmc, текущие
   schemas/ACL/TTL/dedup, actual session ACK и упаковку native. При недоступном
   source не считать контракт проверенным и не имитировать app1 в отчёте.
3. Перед MQTT-клиентом задать: «Какой тип MQTT-клиента создаётся: main_app,
   extra_service или svc_desk?» Не создавать отдельного CONNECT/client_id для FM,
   не менять dev/{SN}/svc и retained svc_online/svc_offline/LWT.
4. Сохранить PB mitigation из release61a14fc: отдельный пул, короткие DB timeout,
   fail-closed503, sampling discovery. Не возвращать FM в payment pool.

## Работы по владельцам

| Владелец | Результат |
|---|---|
| PB/shared | Bounded cross-worker probe state и explicit readiness mode; additive PB migration; mTLS probe-result endpoint; first-valid response; single-use owner/view proof; ACK обновляет freshness в существующей транзакции |
| IoT/app1 | Service-auth probe dispatcher → существующий fmc action=probe, без lease/worker/S3; TTL10 с, QoS1/no-retain; PUBACK не является readiness |
| Native | Удалить startup/idle hello timer; обработать probe с ограниченным TTL/dedup; один HTTPS ответ через Leo4Proxy; оставить active start/renew ACK; после close/expiry никаких FM timers |
| MB/UI | Fleet без readiness fan-out; explicit click→checking→bounded proof→обычный acquire; отмена по view/generation; несовместимость/timeout/503 видимы; никаких auto-retry/acquire |

Новые HTTP формы и поля заданы контрактом. Фактический service-auth маршрут
IoT нужно определить после сверки внешнего source и записать в producer/consumer
матрицу. Прежде кода зафиксировать machine schemas/positive+negative fixtures
для probe/response/proof consume и additivity к существующему protocol2.

## Последовательность

1. Воспроизведение baseline: число FM requests/DB writes в idle и fleet fan-out;
   текущая PB защита уже работает, не приписывать ей устранение polling.
2. PB/shared/API + IoT consumers, затем MB/UI с совместимым legacy selected-only
   path; после них native producer. Новый агент не fallback-ит в periodic hello.
3. Полные проверки каждого изменённого Python consumer/shared и frontend build.
   Native: обязательные x86/x64/default build, packaging/signing/release rules.
4. Разрешённый пилот: один явно указанный terminal/tenant, не массовое включение.
   Release SHA/digest/suite version фиксировать отдельно для каждого владельца.
5. Handoff со сравнением нагрузки, фактическими ACK/lease/stop, fault cases,
   runtime пределами и rollback. Legacy hello удалять отдельным cutover после
   инвентаризации клиентов, а не в первом несовместимом релизе.

## Приёмка

- Idle30 мин:0 FM HTTPS/DB writes; reboot не вызывает hello.
- Fleet10 мин без клика:0 readiness запросов по всему парку.
- Один выбранный терминал: один bounded probe, без lease/file work; metadata helper
  ограничен30 с. Далее обычные
  actor/device policy и IoT conflict admission с single-use proof.
- Active: freshness45 с от проверенного start/renew ACK; отдельного hello нет.
- Close/expiry/error возвращает FM к0 после безопасного stop/drain.
- Duplicate/late/expired/cross-tenant/view/serial/instance replies не меняют чужое
  состояние; restart любого owner не восстанавливает старый proof/lease.
- DB wait не удерживает FM connection на время ожидания агента; payment pool
  остаётся доступным при насыщенном FM pool. Нет новых брокерных топиков.
- Объективный отчёт: build/unit/интеграция/hardware E2E отдельно; PUBACK≠ACK.

## Исходники для начальной сверки

- PB: app/routers/file_manager.py, schemas/file_manager.py, services/file_manager_*,
  file_manager_database.py, dependencies.py и cert_discovery.py.
- Shared: etranprocessing_db/file_manager.py; изменения общей схемы только через PB.
- MB: backend/app/routers/file_manager.py, frontend/src/routes/files/FilesPage.tsx,
  frontend/src/api/fileManager.ts; существующие lease/client consumers.
- Native/app1: конкретные пути из [канонической FM архитектуры](../../../docs/etran_arch-file-manager-remote-windows.md),
  после явного разрешения scope и чтения их локальных правил.

## Текущее evidence

Это исходное задание сохранено как спецификация, не указание заново реализовать
уже принятый код. PB mitigation сохранён; etranprocessing PR158 и IoT PR171
развёрнуты. Native PR13/14, signed suite1.13.66/L4Con1.12.5 и частный kit PR15
установлены на773. MQTT extra_service и повторный UAC пользователь подтвердил
в текущей задаче; повторно спрашивать это в её продолжении не нужно.

Реальные malformed/wrong-view отказы, correlated probe, start ACK, navigation,
browser renew ACK после исходного deadline и close проверены. Fleet UI600 с:
0 FM requests. Native idle1863 с:0 FM child starts; DB idle1801 с/31 sample:
FM rows неизменны. Пользователь сообщил об установке66 на119 и55. Итоговые границы
доказательства и release SHA — в [implementation packet](../completed/2026-11-12-fm-on-demand-release.md).
Следующему агенту: начинать с актуального handoff и оставшихся явно перечисленных
проверок; обновление других терминалов и удаление legacy hello — отдельный scope.
