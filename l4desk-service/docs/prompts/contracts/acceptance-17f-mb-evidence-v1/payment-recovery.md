# L4D-17F-MB-PAYMENT-RECOVERY-01 — актуальное состояние после оплаты

Статус: `VERIFIED_RUNTIME` в изолированном test-backend. Проверка:
2026-09-27 17:20–17:36 UTC. Общий 17F этим отчётом не принимается.

## Найдено и исправлено

Hub показывал tenant 10000 как `blocked`, хотя после mock-платежа 5
баланс был +1000 копеек и admission разрешал сеанс. Повторная оплата
сохраняла баланс и anchor, но не обновляла `FinBillingProfile.entitlement`;
его пересчёт ждал выключенного в тестовом контуре фонового worker.

Commit `4184ee930e869ddfb044029512e51e7a69ed20f6` вызывает существующий
`FinEntitlementService` в транзакции успешного платежа, ручной оплаты и
идемпотентного poll/webhook. Дополнительной проводки нет. Расчёт выполняется
по текущему балансу и штатной границе grace; частичная оплата с оставшимся
долгом после grace сохраняет `blocked`. Anchor не переносится.

## Проверки

- Полный MenuBuilder backend: **529 passed, 51 warnings**.
- Ruff check/format для `app tests` прошли; pyright для `app` и изменённого
  теста — 0 errors, 0 warnings.
- Regression: полная и частичная оплата через mock YooKassa и ручной
  платёж; повтор успешного webhook восстанавливает устаревший cache без
  новой проводки или смещения anchor.
- Изолированный image построен из Git archive SHA-256
  `7c654b65a7a7f4bb04cddc232b773280d1ff99d55c45bd5c22f8430350ae1297`.
  Image ID: `sha256:fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345`.
  OCI revision label содержит `4184ee9`; source archive digest также записан
  в image label. Пересоздан только test-backend; `/docs` — 200.
- Под текущим test05 через обычный login test API два poll платежа 5
  вернули 200/succeeded и ту же transaction 9. Баланс до/после — 1000 коп.,
  projection version 3, last transaction 9, `updated_at` не изменился.
- На обычном сайте Hub finance overview вернул 200/`active`; строка UI
  tenant 10000 показала `10.00 ₽ / Active`. Production image не менялся:
  Hub прочитал исправленное сохранённое состояние из общей БД.

## Корреляция

Текущий публичный Hub API с `tenant_id=10000&session_id=494&payment_id=5`
вернул 200, `overall_status=matched`, пустые `mismatch_codes`. Связаны:
registration 4 → terminal 3722 → PIN consumed → closed session 494
(`entitlement_blocked`) → usage 5 → payment 5 → transaction 9.
Дебет=кредит=10000 копеек. Запрос только по payment 5 сообщает отсутствие
terminal/PIN контекста по строгому контракту 14-MB; эта диагностика
не отключалась и произвольный терминал к платежу не подставлялся.

## Воспроизводимость тестового контура

До обновления часть флагов существовала только в окружении старого
Compose-запуска. В приватном `.env.e2e.local` закреплены пять несекретных
значений: `E2E_BACKEND_IMAGE=l4desk-e2e-test-backend:4184ee9`,
`E2E_POLICY_ENFORCEMENT_ENABLED=true`,
`E2E_FREE_QUOTA_TEST_TENANT_IDS=[1000]`, `E2E_FREE_QUOTA_TEST_SECONDS=600`,
`E2E_TERMINAL_ONBOARDING_ENABLED=true`. Проверено совпадение эффективной
конфигурации с работающим контейнером. Test clock: allowlist `[]`, offset 0;
IoT consumer и глобальные workers выключены. Provider URL указывает на mock.

Rollback только тестового контура: вернуть `E2E_BACKEND_IMAGE` к
`l4desk-e2e-test-backend:e244bae` и пересоздать `test-backend` стандартным
Compose с `--no-build --no-deps`. Старый image
`sha256:247cc1403d08f50ec0f9cdf1d55d2f35f8fc27f34a9b40d398a09701f21603b1`
сохранён. Исправление исходников опубликовано; private env не включён в Git.

Production rollout этого кода остаётся отдельным release-шагом. Runtime
smoke проверяет повтор существующего mock-платежа, а локальные regression
tests — новые и частичные платежи. Реальная ЮKassa не вызывалась.
