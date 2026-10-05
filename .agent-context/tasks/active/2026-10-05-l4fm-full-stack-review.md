# L4FM: сквозное ревью и план улучшения

## Контекст
- Scope: review MB/PB/app1/native tools/Mosquitto/RabbitMQ, разрешённый live test на 1000007;
  результат этой задачи — документация, без изменения production кода/config и без deployment.
- Etran baseline `93a07fba56a205be76e4d9836fd8e8634ef6af39`, working tree без commit.
- IoT checkout `iot-rpc-rest-app-fm`: `c4293c99ea15c7e51910652c547ba52b1a259fec`;
  проверенные основные FM/lease/task/input файлы не отличаются от deployed `f4cd4128e4f0caa33788918692f97603c6061687`.
- Окно live наблюдения: 2026-10-05 17:53–18:15 UTC; позднее завершена собственная console session.
- MCP Ops UNAVAILABLE, использован SSH read-only; preflight RAM2069MiB, root49%, load0.27.
- Запрещённые legacy/SQL каталоги не исследовались. MQTT extra_service выбран пользователем ранее.

## Результат
[Полный отчёт](../../../docs/etran_arch-l4fm-review-and-improvement-plan.md):
19 findings, сравнение существующих средств, расчёт timeout/load, UI и Q0–Q8 план.
Зарегистрирован в docs/README; обновлены базовый FM документ и context contract/index.

## Контракты
- Текущий v1: IoT lease/RPC → l4con, PB tickets/results/transfer authority, S3-only bytes.
- Предлагаемый v2: `srv/{SN}/fmc` / `dev/{SN}/fmr`, bounded MQTT navigation/results,
  app1 correlation/assembler/multi-worker delivery → MB tenant WS. PB policy при активации,
  transfer integrity/commit остаются в PB; все agent HTTP через Leo4Proxy.
- Пользователь выбрал отдельный канал и автоматическое развёртывание актуального Mosquitto config
  новым l4setup вместо сохранения старого. План включает общий generator/version с l4superv,
  candidate validation, replacement, health check/rollback, fresh/upgrade/repair compatibility.
- Upload только ordinary desktop token; policy-authorized privileged read для download,
  без privileged destination write. Общая монополия files/console/video/input сохраняется.
- Эти изменения **не реализованы** данным review; новый queue/binding не создавался.

## Проверено
- [x] Код producer/consumer MB, PB, app1, l4con, Leo4Proxy, l4setup/l4superv и packaging.
- [x] Browser через playwright-cli: start/list, parent-path отказ, ложная connection-lost ошибка,
  stale list после upload; один alert воспроизведён, двойной alert не подтверждён.
- [x] Первая 48-byte upload failed; следующая ручная upload/download successful,
  SHA-256 `AF7607FB1D1F736D33D0724D9062C95AED6004F4F681328F576544EAD0933A23` совпал.
- [x] Runtime root/file ACL SYSTEM/BA и Hidden у загруженного файла подтверждены;
  desktop-user upload текущая реализация не обеспечивает.
- [x] Текущие bridge routes прочитаны через разрешённую консоль; broker own-SN permissions
  проверены read-only, существующие out/ctl/req/res очереди имели по два consumer.
- [ ] Native phase failure первой upload: ticket/grant получены, commit отсутствует;
  точного Win32/HTTP/IO error нет, требуется типизированная native диагностика.
- [ ] Redis acquire/revoke interleaving: static finding, deterministic concurrency test не выполнен.
- [ ] Fault matrix, x86/Win7, 100-cycle soak, policy-loss/large-file/payment load не выполнены.
- N/A: backend tests/lint/build — изменения только документационные, запрещены scope rules.

## Evidence
Подробные correlation IDs, версии и ограничения в отчёте, без auth headers/URL grants/credentials.
Parent failure 1000007: operation `f5e16448-a38b-45ba-879a-778ca5543fcf`,
cancel200 затем stop409 в 17:58:29 UTC. Первая upload `386e50ac-9021-4693-83ff-5cc6e3378c6a`.
Ложная потеря связи объясняется cleanup-классификацией; это не доказательство network outage.

## Cleanup и продолжение
- Созданный fixture на терминале удалён после проверки точного имени и hash;
  подтверждены FM_FIXTURE_REMOVED=True и FM_ORPHANS=0. Чужие файлы не менялись.
- Собственная console lease отключена; test account logout выполнен; браузер fm-review закрыт.
  Команда delete-data сообщила, что сохранённых user data для этой сессии нет.
- S3 lifecycle остаётся механизмом удаления объектов попыток; немедленное удаление не подтверждалось.
- При реализации сначала закрыть lease race/cleanup/worker liveness; v2 protocol и additive server
  topology подготовить до signed native rollout. Не пересобирать опубликованные подписанные артефакты
  под прежней версией и не считать этот review полным E2E подтверждением надёжности.
