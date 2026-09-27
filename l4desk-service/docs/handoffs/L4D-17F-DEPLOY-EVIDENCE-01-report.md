# L4D-17F-DEPLOY-EVIDENCE-01 — текущие версии и оставшиеся разрывы

Статус: `PARTIAL_RUNTIME_EVIDENCE`, общий 17F — `BLOCKED_TESTS`.
Сверка 2026-09-27 17:20–17:38 UTC через read-only SSH, Alembic current,
SHA-256 исходников и browser API. Это data-only export для контроллера.

## Матрица

| Компонент | Работающий image ID | Связь с source / schema |
|---|---|---|
| ProcessingBackend | `sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511` | 59/59 Python-файлов `app` + `alembic` совпадают с Git `083138f223b723098e9a188803e3fc802e8a6011` после CRLF→LF; schema **027 (head)**. |
| IoT app1 | `sha256:29aa88169ab8b051705044f0feb1ad8b4d5af5827bcef5294b437c7e29b9c431` | 147/147 Python-файлов приложения (без tests/.venv) совпали по SHA-256 с серверным checkout `60f7762ec766e432bf372e255e94fdd33d3d91d2`; tracked tree чист, вне Git только logs. Schema **0008_org_reservations (head)**. |
| MenuBuilder production | `sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090` | 92/92 файлов `app` совпали по SHA-256 с release archive `fb2273c0bb633426067b2a9487538d84155ef446`; общая schema 027. |
| MenuBuilder test | `sha256:fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345` | Собран сейчас из `4184ee930e869ddfb044029512e51e7a69ed20f6`; source archive и revision закреплены в labels; общая schema 027. |
| Media ingress | `sha256:f201ff382eaa9ca127681cee16314e86deffa9d2441f6c293f3e10ff57e61c9f` | Binary SHA-256 `1e7f48b11b518ebba4e63eaf84326632e1da1dac4cc024f8767490510be20f91`. Серверный `l4media_ingress.c` имеет Git blob `6ef418364b9de200a44cff8feb24852e3196ba4a`, найденный в `c200d60485d32c805ae00530c57ad7a4b6b2b1ce`; единственный файл не доказывает полный build provenance. |
| Agent 1000007 | Live binary fingerprint ещё не получен | Исторический online/video/PIN smoke имеется; конкретный исполняемый файл на тестовом терминале к release digest пока не привязан. |

Для PB сохранён [поимённый fingerprint](evidence/17f-deploy-20260927/pb-source-fingerprint.json):
raw SHA-256 каждого файла внутри image и Git blob SHA-1 после нормализации
только CRLF→LF. Сравнение с `git ls-tree -r 083138f --
ProcessingBackend/backend/app ProcessingBackend/backend/alembic` дало
59 совпадений, 0 лишних/отсутствующих файлов. Различия перевода строк не
выдаются за raw byte equality. Lockfile/системные зависимости этим сравнением
не проверены. Все серверные контейнеры при исходной сверке `running`,
restart count 0; test-backend затем штатно пересоздан для исправления оплаты.

## Флаги и rollback

Production MenuBuilder: policy, registration, billing, UI, YooKassa, IoT
consumer и оба финансовых workers выключены; onboarding и financial core
включены. Free quota — 7200 секунд, test allowlist `[]`; test clock settings
в старом production source отсутствуют.

Test MenuBuilder: policy/registration/billing/onboarding/YooKassa включены,
provider — mock; quota `[1000]`/600 с; entitlement test allowlist `[]`, offset
0; IoT consumer и оба workers выключены. Конфигурация следующего Compose
запуска закреплена, см.
[отчёт восстановления после оплаты](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17F-MB-PAYMENT-RECOVERY-01-report.md).

Проверено наличие rollback image:

- MenuBuilder: `sha256:c70a11d88eef2697898d936278d4f8ce8066bc8cd0fa951d7ece9efda9b49e8e` (`rollback-fb2273c`).
- IoT: `sha256:4c47ef6c80eacc4a0ac882836ebc5a97e442cc2f0cc4a8a76c5d84e8734252fc` (`rollback-before-online-enrichment`).
- Media: `sha256:14845752d37ed413b0c0c8af56455f61dbb084fde5b00d394612123076563373` (`pre-stop-773-20260926`).
- Test backend: `sha256:247cc1403d08f50ec0f9cdf1d55d2f35f8fc27f34a9b40d398a09701f21603b1` (`e244bae`).

Наличие image не доказывает безопасный downgrade схемы: rollback не
выполнялся. Для PB отдельный предыдущий rollback tag не обнаружен.

## Что ещё требуется контроллеру

1. Live fingerprint Agent и полная привязка media image к исходному build.
2. Явный план совместимого release/rollback для оставшихся компонентов;
   исправления grace и оплаты пока находятся только в test-backend.
3. Итоговый verdict по 120-минутному наблюдению и эквивалентности коротких
   проверок TTL; новые часы ожидания не назначались.
4. Граница архивной приёмки: producer restore и consumer contract прошли,
   а импорт/просмотр реального архива в deployed Hub и production volume
   остаются непроверенными. Трёхлетнее ожидание исключено пользователем.

Hub correlation на существующих фактах пройдена: tenant 10000, session 494,
payment 5, transaction 9 — `matched`, debit=credit=10000 коп., HTTP 200.
Mock payment replay сохранил balance/version/transaction и восстановил
`active` в UI. Новый tenant, платёж или видеосеанс в этом шаге не создавался.
