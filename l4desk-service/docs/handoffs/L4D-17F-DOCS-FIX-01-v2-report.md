# L4D-17F FIX — итоговый пакет приёмки v2

Producer verdict: `ACCEPTED` для согласованной технической приёмки 17F.
Controller handoff status: `PENDING` до независимой записи в журнале.
Этот отчёт сам по себе не принимает `H-L4D-17F-DOCS-FIX-01-v1` и не
открывает 18A. Исторические отчёты `BLOCKED_TESTS` не изменены.

## Task intake и границы

- Владелец решения: независимый контроллер каскада; producer этого
  пакета — `l4desk-service`, consumer — `L4D-18A-SHARED`.
- Входы: три ранее принятых data-only контракта 17ABC/17D/17E и
  адресный экспорт корректирующих проверок 17F. Sequence gate —
  `H-L4D-17E-MB-FIX-01-v1`.
- Инварианты: tenant ownership, единственный console/video lease,
  stop текущего epoch, один финансовый posting на источник,
  debit=credit, округление и недоказанный хвост в пользу клиента.
- Пользователь разрешил существующие test04/tenant 1000 и
  test05/tenant 10000, mock YooKassa, короткие периоды и исправления.
  Новых tenant для финализации не создавали.
- Пользователь отдельно принял **Agent `1.8.2-beta-1` как есть**.
  Агент не изменялся и не пересобирался. Текущая версия на тестовом
  терминале принята по свидетельству оператора; это не новое измерение
  hash исполняемого файла 1000007.
- Реальный платёж и ожидание трёх лет исключены пользователем из
  технического gate. Поля retention и финансовые инварианты сохранены.
- Итог — приёмка механики перед production rollout, а не включение
  коммерческих флагов для всех клиентов и не закрытие всего каскада.

Регистрация: `R-L4D-17F-DOCS-FIX-01-v2`. Точные входные commits,
версии, Git/raw SHA-256 и публикация проверяются в
[протоколе gate](evidence/17f-deploy-20260927/final-contract-gate.json).
Каждый вход имеет единственный `ACCEPTED`, действующий адресный допуск;
принятые входы не отозваны. V1 registration отозвана только ради новых
неизменяемых путей R/C; старый runtime report остаётся историческим.

## Итоговая матрица

Все времена UTC; runtime-проверки 2026-09-27, кроме отдельно указанного
двухчасового наблюдения 2026-09-26. «Историческое» означает опубликованное
evidence предыдущего прогона, а не повторный тест в момент этой фиксации.

| Критерий | Evidence и результат | Уровень |
|---|---|---|
| Self-registration → email → tenant | Test05: registration 4, org 10000 зарезервирован в IoT и MenuBuilder без коллизии; owner role 5; пользователь подтвердил письмо, повтор ссылки сообщил «уже подтверждено». Владелец запретил новые tenants, поэтому использован этот завершённый сценарий. | PASS, историческое E2E из принятого экспорта |
| Terminal → PIN → Agent | Штатный owner onboarding создал бесплатный 1000007, terminal PK 3722; PIN установлен оператором, Agent online, desktop available, 1920×1080. Hub связывает registration 4, terminal 3722 и consumed PIN. | PASS, runtime и оператор |
| Старый пользователь и ownership | Test04/tenant 1000: auth/me 200, role 5, существующий 1000005 и статусы 200. Неверный org отказан, после правильного login quota отказал отдельно. Test05 не получил admin Hub: 403; тестовый superuser получил 200. | PASS, black-box 17F + принятый 17E |
| Console/video exclusion | Во время видео console lease 409, видео продолжилось. После штатного stop retry получил lease; `ver` возвратил Windows version и exit 0. | PASS, 13:01–13:15 |
| Video/stop/retry | 10-минутный поток пересёк TTL 600: player time 602, 7332 decoded frames, RTP/bytes растут, fresh RTP. Среди 161 проверенного запроса не было 500. Stop освободил lease, active session и media route; следующий start/stop успешен. | PASS, black-box |
| Длительность 120 минут | Session 462, 1000003: 18:01:26–20:06:18 26 сентября, 7492 с; живое видео, fresh RTP, unrouted 0, штатный stop. Повтор 465: 61 с, usage +61, без повторного учёта длинного сеанса. | PASS, исторический soak; эквивалентность TTL ниже |
| Порционный учёт / free quota | Tenant 10000 с временным лимитом 120 с: usage 62→124 до stop, после stop 179 с, billable 0. Новый lease 403 `free_quota_exceeded`. Тест проверяет механику счётчика с коротким лимитом; базовые 7200 с не менялись глобально. | PASS, runtime |
| Mock payment / duplicate | Payment 4: два webhook 200, poll 200; одна transaction 6, debit=credit=1000 коп., balance version 1. После оплаты lease 201, DELETE 204. | PASS, runtime mock |
| Monthly charge / duplicate event | Подтверждённый online 1000006 после anchor: charge 3/transaction 8 на 10000 коп. Повтор event вернул те же IDs, баланс −9000 коп. Не enrolled terminal после исправления даёт 409 до проводки. | PASS, isolated runtime |
| Grace → blocked → stop | Session 494: до deadline grace, stopped 0; после deadline blocked, stopped 1, reason `entitlement_blocked`. Закрыта 16:28:49; оператор подтвердил остановку живого видео. Новый lease 403. Сетевой сбой session 493 отдельно классифицирован: он не выдаётся за успешный stop живого видео. | PASS, адресный test clock + оператор |
| Восстановление после платежа | Payment 5/transaction 9, balance +1000 коп., lease 201/DELETE 204. После исправления `4184ee9` два poll 200 сохранили transaction 9, balance version 3, updated_at; Hub показал Active. Partial payment с оставшимся долгом покрыт regression, не новым runtime списанием. | PASS, isolated runtime + local regression |
| Округление / целостность | Подтверждённые периоды учитываются порциями; неподтверждённый хвост при сетевом отказе прощён. Golden tests границы 120m/120m01s и rounding приняты в 17E; повторные online/webhook/poll не породили новую проводку. Дневные 879 с включают предыдущие сеансы и не выдаются за отдельную длительность session 494. | PASS, runtime примеры + локальные граничные тесты |
| Hub correlation / mismatch | Полный запрос tenant 10000/session 494/payment 5: 200/matched, mismatch []; registration→terminal→PIN→closed session→usage 5→payment→transaction 9, debit=credit=10000. Payment-only запрос сохраняет `TERMINAL_NOT_FOUND`/`PIN_PROVISIONING_MISSING`; произвольный terminal не подставляется. | PASS, публичный Hub |
| Reconciliation retry | Scoped run tenant 1000 matched/0 mismatches; повтор operation ID создал отдельный audit run. Это не новый финансовый posting; уникальность ledger подтверждена отдельно. Контракт не обещает один audit run на operation ID. | PASS с явно указанной семантикой |
| Archive manifest / restore | 6 синтетических записей, dry_run=true/purge=false, verified manifest и два checksum; restore 6/6 в другую SQLite, hot 6/6 и summary сохранены. Виртуальная дата ускоряет проверку периода. Consumer импортировал **тот же output**: один batch при replay, checksum conflict отказан, физический путь скрыт. | PASS, безопасный producer runtime + consumer contract |
| Версии / flags / rollback | Матрица ниже, PB schema 027, IoT 0008; коммерческие флаги production выключены. Media binary воспроизведён из Git и совпал с live. | PASS для pre-rollout baseline |

## Закрытие обнаруженных дефектов

1. Hub 404 и Decimal/float 500: `fb2273c`, публичные API/UI 200,
   tenant role 5 admin API 403; video start/stop после deploy без 500.
2. Неверный stop при grace: `9003400` использует текущий app1 epoch;
   live session 494 остановлена, lease admission отказан.
3. Metering неизвестного terminal давал FK/500: `e244bae` возвращает
   409 `terminal_not_enrolled` до финансового изменения.
4. После оплаты persisted entitlement оставался blocked: `4184ee9`
   пересчитывает его в payment transaction, включая replay. Исправление
   проверено на test image и общем read path Hub, без новой проводки.
5. Неполный media build provenance: независимая сборка `c200d60`
   воспроизвела live executable. Отстававший host unit test доставлен
   из Git archive; следующий штатный build включает renew/stop проверки.

Критических незакрытых дефектов в проверенной **release-кандидатной**
конфигурации не осталось. Production пока содержит прежний MB `fb2273c`:
grace/payment fixes проверены в isolated image и должны быть доставлены
в 18E **до** включения commercial policy. Это явная граница deploy,
а не утверждение, что production уже обновлён.

## Версии и артефакты

| Компонент | Source / schema | Работающий image либо принятый package |
|---|---|---|
| Agent | `1.8.2-beta-1`, без изменений, решение владельца | Проверенные x64 package hashes: l4capture `6014aa2c12804c0e71335290e9b2fdb6baed297fa3eec5b8028c7e864c8257e2`; l4desk `03f989b94de0ed9467d75f105d36862aa8378e26075b58b5a7e9e0360a46ec90` |
| ProcessingBackend | `083138f223b723098e9a188803e3fc802e8a6011`, schema 027 | `sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511` |
| IoT app1 | `60f7762ec766e432bf372e255e94fdd33d3d91d2`, schema 0008_org_reservations | `sha256:29aa88169ab8b051705044f0feb1ad8b4d5af5827bcef5294b437c7e29b9c431` |
| Media ingress | Executable from `c200d60485d32c805ae00530c57ad7a4b6b2b1ce` | `sha256:f201ff382eaa9ca127681cee16314e86deffa9d2441f6c293f3e10ff57e61c9f` |
| MenuBuilder production | `fb2273c0bb633426067b2a9487538d84155ef446`, schema 027 | `sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090` |
| MenuBuilder candidate / test | `4184ee930e869ddfb044029512e51e7a69ed20f6`, schema 027 | `sha256:fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345` |

PB: 59/59 Python app/Alembic sources match after CRLF→LF; IoT 147/147
app Python files and MB production 92/92 app files match their sources.
PB lockfile/system dependencies не покрыты этим сравнением.
Test image закрепляет Git revision и SHA-256 source archive в labels.
Media executable SHA-256 в live и rebuild совпадает точно; image целиком
не объявляется воспроизводимым. Подробности и per-file evidence:
[deployment](L4D-17F-DEPLOY-EVIDENCE-01-report.md),
[media provenance](L4D-17F-MEDIA-PROVENANCE-02-report.md).

Последняя read-only сверка: все пять контейнеров running, restart count 0.
Production: policy/registration/billing/UI/YooKassa/IoT consumer и оба
financial workers=false; onboarding/financial core=true; quota 7200,
test allowlist []. Test: policy/registration/billing/onboarding/mock=true,
quota [1000]/600; entitlement test allowlist [], offset 0; IoT consumer
и workers=false. Следующий Compose воспроизводит эти значения.

## Проверки, которые сохраняет релиз

- MenuBuilder `4184ee9`: **529 passed**, 51 warnings; Ruff app/tests
  pass; Pyright app и изменённый тест — 0 errors/0 warnings. Полный
  pyright всех старых тестов не объявляется зелёным.
- Archive producer: 14 tests pass; manifest schema errors=[];
  consumer regression входит в 529, использует реальный output producer.
- Media из Git archive: все семь C test groups PASS, native Linux build
  успешен, совпадение live binary SHA-256.
- Agent baseline: опубликованные x86/x64 builds; l4capture 129/129,
  l4desk 21/21, package manifest/rollback проверки. Эти проверки
  исторические; новый Agent build для 17F не запускался.
- Финальный документационный шаг: ссылки, Git/raw hashes, contract gate,
  append-only history и secrets scan; backend тесты повторно не запускаются.

## Release / rollback и оставшиеся задачи rollout

Порядок: 18A shared/schema → 18B PB → 18C IoT → 18D media → 18E MB →
18F эксплуатационный реестр. Кандидат MB `4184ee9` включает исправления
grace/metering/payment; нельзя брать только старый production `fb2273c`
и включать policy. Agent `1.8.2-beta-1` сохраняется без пересборки.

Rollback до следующих шагов:

- PB: текущий image закреплён `user1-processing-backend:17f-accepted-baseline`.
- IoT: текущий ID сохранён; предыдущий image
  `sha256:4c47ef6c80eacc4a0ac882836ebc5a97e442cc2f0cc4a8a76c5d84e8734252fc`
  существует, но откат к нему требует проверки schema 0008 compatibility.
- Media: сохранить текущий `f201ff...`, source `c200d60` и его renew/stop
  contract. Старый `148457...` существует, но не рекомендован как default
  rollback: нельзя потерять исправление TTL/stop.
- Production MB: вернуться к текущему `e0d17a...` с commercial flags=false;
  после такого возврата acceptance новых paid/grace функций не заявлять.
  Test rollback `247cc140...` сохранён только как диагностический fallback:
  он ещё без payment recovery fix.
- Вначале закрыть admission/выключить затронутые worker flags, штатно
  завершить или дренировать активные сессии, вернуть проверенный image и
  совместимые настройки; затем health/ownership/start-stop smoke.
  DB schema не понижать и ledger не править. Реальный rollback в этом
  прогоне не выполнялся; его порядок проверяется project-local в 18*.

Production archive volume/permissions/backup readiness и archive worker
относятся к 18D; `/mnt/l4desk-archive` сейчас не отдельный mount, worker
выключен. Purge запрещён до их проверки. Consumer archive check пока
использует DB double и не является импортом в deployed Hub/PostgreSQL.
Эта граница не скрывает дефект: обязательный 17F dry-run/restore пройден,
а готовность production storage ещё не объявляется.

Живая real-YooKassa проверка, доставка grace email, Windows 7/POSReady x86
runtime, массовые 100 start/stop циклов и production backup restore не
проводились. Они не выдаются за результаты этой согласованной приёмки.

## Evidence и передача

- [Первый black-box прогон](L4D-17F-DOCS-FIX-01-report.md),
  [free/payment дополнение](L4D-17F-DOCS-FIX-01-supplement-2026-09-27.md),
  [Hub correction](L4D-17F-MB-HUB-FIX-01-report.md).
- [Archive producer](L4D-17F-MEDIA-ARCHIVE-EVIDENCE-01-report.md),
  [fixture manifest](evidence/17f-archive-20260927/manifest.json),
  [restore result](evidence/17f-archive-20260927/summary.json).
- Адресный data-only export корректирующих MB reports, исторического
  soak, Agent baseline и регистрации — четвёртый input в gate JSON.
- Test clocks возвращены к zero/empty allowlist; временные browser auth
  sessions и SSH tunnel закрыты в runtime-шагах; новые сеансы здесь не
  открывались. Existing tenants/terminals/payment/ledger сохранены как
  согласованные test facts. Проверенные release sources/images и
  обезличенные evidence сохранены для воспроизводимости.

Окончательный candidate публикуется отдельным commit после этого отчёта:
[L4D-17F-DOCS-FIX-01-v2-candidate.md](L4D-17F-DOCS-FIX-01-v2-candidate.md).
Точные R/C и SHA-256 передаются независимому контроллеру; до его записи
статус handoff остаётся PENDING и следующий основной этап не запускается.
