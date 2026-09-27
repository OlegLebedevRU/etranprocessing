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
| Video/console | 1000003/1000005 ранее проверены; на image `9bda9ce` владелец подтвердил watch WS 101, status 200/200 и start/move/stop 1000005 без задержки и 500. Session 484 closed; console/409 — historical E2E. | E2E pass для video |
| Финансовая целостность | Schema 027; tenant 3 ledger debit=credit=1100 коп., balance 900; tenant 1000 debit=credit=1000, balance 1000. `FinReconciliationService` на 20-минутном окне tenant 1000: matched, mismatch=0, projection difference=0. | Runtime/read-only pass |
| Остальные project-local сценарии | DST/month/last day, grace/block/late payment, manual payment/storno, rounding/discarded, Hub filters/correlation/mismatch, archive import/retention, rebuild проверены backend suite. | Local pass; отдельный runtime E2E не выполнялся |
| Consumer fixture | Принятый IoT provider fixture v1.1.0 из commit `22a50a1` сохранён без изменения (SHA-256 `771856c6cfed996aa8a9a99c122a73096afee123a089895f0089c17eb6bac1fe`); старый v1.0.0 сохранён, payload примеров одинаков. | Local contract pass |
| Меню и backend | Backend suite 519 passed, 51 warnings; Ruff/format/Pyright exit 0; frontend 60 tests passed, build exit 0. | Local pass |

## Развёртывание и откат

- Чистый Git archive backend source `df7598c3f1830c86df585c380989e6b278b3a2b3`
  имеет SHA-256 `a1eeb8268221fcf50ea8e0f6014115cbccf15d2c871c633d2e4e323effbb3150`.
  Image `sha256:42fa63c0495d71e4e9700564595aba9a4506f3ea6cefde4698f0b822223e301b`
  был первоначально запущен в изолированном и production backend;
  на том этапе 92/92 Python-файла production контейнера совпали с архивом,
  лишних и отсутствующих не было. Текущий image описан ниже.
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
- В ходе проверки роли владельца обнаружено два ограничения video UI:
  frontend не запускал status/watch для роли 5, затем browser watch WebSocket
  возвращал 403 из-за отсутствия отдельного `video:view` в JWT. Frontend
  исправлен в commit `20cee55`, 60 тестов и build прошли; 41 новый asset
  установлен без перезаписи прежних assets, index переключён на
  `index-ChG1nb5J.js`. Backend watch исправлен в commit `9bda9ce`:
  владелец роли 5 допускается по той же ролевой политике, что REST, viewer
  по-прежнему требует явного `video:view`, принадлежность terminal tenant
  проверяется до подключения к app1. Backend suite: 519 passed;
  Ruff/format/Pyright чистые. Git archive `9bda9ce` только из `shared/` и
  `MenuBuilder/backend/` имеет SHA-256
  `0910dc799ac924213265dba4ca6fd3295f48f90e8cec197b98d7be62258afddf`.
  Оба backend-контейнера после отдельного подтверждения пользователя запущены
  из image `sha256:c70a11d88eef2697898d936278d4f8ce8066bc8cd0fa951d7ece9efda9b49e8e`;
  Все 92/92 Python-файла `app/` в обоих контейнерах побайтно совпали с
  Git archive (mismatch/missing/extra=0). `/docs`=200,
  restart count=0. Предыдущий image оставлен под tags
  `user1-menubuilder-backend:pre-watch-9bda9ce` и
  `l4desk-e2e-test-backend:pre-watch-9bda9ce`. Production policy/billing
  выключены; изолированный backend: policy=true, tenant=[1000],
  quota=600 с, billing=true. На обычном сайте под test04 watch WebSocket
  вернул 101, `control/status` и `stream/state` — 200; пользователь затем
  подтвердил движущееся видео и штатную остановку без 500. Read-only SQL:
  session 484 `closed`, active sessions tenant 1000=0, source/free usage
  выросли с 1048 до 1074 с, billable=0, ledger 1000/1000 коп. и balance
  1000 коп. остались неизменны.
  Для detached candidate SHA-256 отдельных `artifact_paths` считается от
  Git blob с LF; Windows Git archive при `core.autocrlf=true` содержит CRLF,
  поэтому его побайтный SHA-256 для `.py` отличается от Git blob без
  содержательного изменения исходника.
- После адресного rollout выяснено: Compose интерполировал `$admin` и
  `$hash` внутри приватного `AUTH_USERS` MenuBuilder. До исправления
  effective значение в контейнере было короче исходного. Локальный
  `compose.yaml` исправлен в commit `7f9d659` (`env_file.format: raw`),
  затем с отдельного согласия тот же двухстрочный diff внесён в
  `/home/user1/compose.yaml`. Backup:
  `/home/user1/.l4d-releases/compose-pre-auth-users-20260927.yaml`.
  Проверка Compose после изменения прошла без предупреждений; повторно
  пересоздан только `menubuilder-backend`. Effective `AUTH_USERS` содержит
  корректно декодированные два `$$` → `$`, один пользователь разбирается;
  `/docs`=200, image прежний, restart count=0 и коммерческие флаги false.
  Read-only ledger/balance tenant 3/1000 и отсутствие active sessions
  повторно подтверждены. Значение `AUTH_USERS` и хеш не выводились.

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
