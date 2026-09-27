# L4D-17E-MB — повторная приёмка коммерческого контура

```yaml
prompt_id: L4D-17E-MB
scope_project: MenuBuilder
output_handoff_id: H-L4D-17E-MB-v1
status: READY_FOR_CONTROLLER_REVIEW
deployment_status: DEPLOYED_WITH_COMMERCIAL_FLAGS_DISABLED
candidate_format: DETACHED_V1
next_prompt_id: L4D-17F-DOCS
```

Это новый срез после исторического
[BLOCKED_CONTRACT](L4D-17E-MB-report.md). Контроллер ещё не принимал
`H-L4D-17E-MB-v1`; этот отчёт сам по себе не включает коммерческую политику.
Подробные результаты, включая пропущенные проверки, перечислены в
[матрице](../../../../.agent-context/tasks/active/2026-09-27-l4d-17e-acceptance-matrix.md).

## Контракт и границы

- MenuBuilder владеет регистрацией, remote-session usage, ledger и
  entitlement. ProcessingBackend владеет миграцией общей БД; app1 даёт
  lease/event feed, l4media — медиатракт. Required provider handoff ID
  проверены по принятому журналу `l4desk-service/docs/prompts/contract-handoff.md`.
- Сохранены инварианты: один org ID в MenuBuilder/IoT; только владелец
  терминала получает lease; одна posted проводка на один идемпотентный
  источник; debit=credit; подтверждённые секунды учитываются внутри
  сессии, недоказанный хвост прощается потребителю.
- Периодические механизмы проверялись короткими окнами: quota 600 секунд
  только для tenant 1000, локальные cycle/grace 30/10 минут, reconciler
  20 минут. Production календарь и глобальное время не менялись.

## Проверено

| Слой | Evidence | Вывод |
|---|---|---|
| Регистрация и identity | test05 создал tenant/IoT org/reservation 10000, owner role 5; повтор ссылки идемпотентен. Занятый IoT org 4 отклонён 409. | E2E pass |
| Бесплатная квота | Под правильным test04/tenant 1000 clean test image отказал `403/free_quota_exceeded`; новая session и проводка не созданы. | E2E pass |
| Mock payment → paid admission | Mock ЮKassa: payment 3 на 1000 коп., два webhook 200, poll 200/succeeded/transaction 5. Ровно одна posted проводка, balance +1000 коп. Последующий stream lease 201, release 204. | E2E pass, реального списания нет |
| Usage, сутки, metering | Исторические порции session 481: 62+62+62+1=187 с без повторного начисления; переход локальных суток и повтор close worker проверены; новый production smoke session 483 закрылась, source seconds 918→1048, billable=0 при выключенном billing. | E2E pass в заявленных режимах |
| Video/console | 1000003/1000005 ранее проверены; на окончательном production backend image пользователь подтвердил start/move/stop 1000005 без задержки и 500, session 483 closed. | E2E pass для video; console/409 — historical E2E |
| Финансовая целостность | Schema 027; tenant 3 ledger debit=credit=1100 коп., balance 900; tenant 1000 debit=credit=1000, balance 1000. `FinReconciliationService` на 20-минутном окне tenant 1000: matched, mismatch=0, projection difference=0. | Runtime/read-only pass |
| Остальные project-local сценарии | DST/month/last day, grace/block/late payment, manual payment/storno, rounding/discarded, Hub filters/correlation/mismatch, archive import/retention, rebuild проверены backend suite. | Local pass; отдельный runtime E2E не выполнялся |
| Consumer fixture | Принятый IoT provider fixture v1.1.0 из commit `22a50a1` сохранён без изменения (SHA-256 `771856c6cfed996aa8a9a99c122a73096afee123a089895f0089c17eb6bac1fe`); старый v1.0.0 сохранён, payload примеров одинаков. | Local contract pass |
| Меню и backend | Backend suite 518 passed, 50 warnings; Ruff/format/Pyright exit 0; frontend 58 tests passed, build exit 0. | Local pass |

## Развёртывание и откат

- Чистый Git archive backend source `df7598c3f1830c86df585c380989e6b278b3a2b3`
  имеет SHA-256 `a1eeb8268221fcf50ea8e0f6014115cbccf15d2c871c633d2e4e323effbb3150`.
  Image `sha256:42fa63c0495d71e4e9700564595aba9a4506f3ea6cefde4698f0b822223e301b`
  запущен в изолированном и production backend; 92/92 Python-файла
  production контейнера совпали с архивом, лишних и отсутствующих нет.
- Production `menubuilder-backend` пересоздан отдельно 2026-09-27 около
  09:54 UTC. Файлы сервера, Compose/env и БД не менялись. Effective flags:
  registration=false, billing=false, policy=false, entitlement worker=false,
  metering close worker=false, IoT consumer=false. `/docs`=200,
  container running/restart=0. Test-backend оставлен с policy=true только
  для tenant 1000, quota=600 с, остальные коммерческие фоновые workers=false.
- Старый production image сохранён как
  `user1-menubuilder-backend:pre17e-20260927`
  (`sha256:08b6d35b2a1322e3f709ae4864ee4308210fa3cc04c7c3ac1b7ed61352880d21`).
  Откат: вернуть этот tag на `user1-menubuilder-backend:latest` и
  пересоздать только `menubuilder-backend` с `--no-build --no-deps`.
- На серверном Compose при адресном rollout были предупреждения о
  переменных `admin` и `hash`; остальные сервисы не пересоздавались.
  Перед полным Compose rollout надо найти источник интерполяции.

## Остаточные риски и verdict

- Manual payment/storno, Hub, archive, runtime graceful block активного
  потока и projection rebuild имеют локальные тесты, но не отдельный
  изолированный HTTP/browser E2E. Реальный YooKassa не использовался.
- Production коммерческие флаги остаются выключенными. Чистый image и
  видеопуть подтверждены, но production billing admission не включался.
- Frontend index отличается от локального форматированием, однако ссылки
  и SHA-256 всех пяти загружаемых assets совпали. Внешние provider версии
  опираются на принятые handoff и исторические E2E.

**Verdict MenuBuilder:** project-local и адресный consumer-contract smoke
готовы к независимой проверке. `H-L4D-17E-MB-v1` можно принять только
после сверки detached candidate контроллером; до этого 17F не считать
автоматически разрешённым.
