# Identity flow — production release, 2026-11-16

Разрешены push, builder и production deployment. Выпуск завершён через builder →
registry → production pull по immutable digest, без сборки на production.
[MCP Ops Readiness: UNAVAILABLE]: инструменты отсутствуют; SSH fallback preflight
пройден на обоих хостах, ресурсные пороги соблюдены.

## Источники и образы

- etranprocessing/main: `231bf2599fa3fada5140b3a3fcdf4cc0a68435fb`.
- iot-rpc-rest-app/master: `ff9bd14589f156566326f3e36295d3457cd27b87`.
- PB digest: `sha256:ca29cda60c52c349f6080e6dc6c76e90a836f7a786a5ebd849a7c659e98e0530`.
- MB backend digest: `sha256:d025634abb3b3d9e2a8924905e11e9a7934a7466aff45538624f7f1cc830eda3`.
- MB frontend digest: `sha256:b8b4ff347f74fc3ff8c54bee2e9108249fb2783d54de90ae50200678ee6db979`.
- app1 digest: `sha256:6c6134eeba0264b8846a19f69bb91e86a48aacf79e6f0941bfdb6cac0ef2ae5f`.

Использованы чистые актуальные main/master, исходный dirty checkout не очищался.
PB patch конфликтовал только импортом UUID; новые изменения main сохранены.
Пользовательский diff certificate_renewal.py не включён в release patch.
Адресный tracked `deploy/identity_release.py` сохраняет текущую цепочку Compose
файлов, проверяет environment/mounts/network/соседей и откатывает образ при ошибке.
У PB добавлены только IDENTITY_CHANGE_IOT_URL/KEY из серверного service config;
ключ не выводился и не переносился в Git. У MB/app1 environment неизменен.

## Проверено на builder

- PB: Ruff, format, Pyright;421 passed,3 skipped.
- MB: Ruff, format, Pyright;1237 passed,25 skipped.
- IoT:1231 passed,39 skipped.
- Frontend:161 passed, production build.
- Локальный browser flow:3 passed до публикации.
- Read-only secret scan релизного diff: совпадений нет.

## Проверено в production

- app1 Alembic head:0014_task_tenant_history; PB Alembic head:036.
- PB /api/health200; MB /openapi.json200; app1 /docs200.
- Новый MB admin device-events/count без auth:401.
- PB→IoT service request с существующим ключом и запрещённым same-tenant body:
 409, без ключа403; этот запрос не выполняет SQL/передачу устройства.
- Image SHA/digest и ожидаемые runtime revisions сверены release helpers.
- RestartCount0 у всех трёх backend после rollout.
- Environment и mounts сохранены (PB — только два согласованных добавления).
- На каждом шаге IDs соседей проверены; nginx, broker, media, signer, MCP,
  camera-runtime не пересоздавались.
- Static frontend опубликован штатным deploy.py; nginx-default не перезапускался.
- index SHA256:a98ce8780283f7bc88c7722b21670949fbb46753362e39b4672d41ae087374c6.

## Evidence / ограничения

На production: `.etran-ci/runs/identity-change/<source-SHA>/<component>/verified.json`;
frontend штатный `.etran-ci/menubuilder-frontend-release.json`.
Registry artifacts на builder: `etran-ci/artifacts/<component>/<source-SHA>.json`.
До health startup несколько Connection refused ожидаемо повторялись; итоговые
health проверки успешны. Повторный ручной MB /api/health дал404: штатный endpoint
проверки — /openapi.json, он200.
Реальное устройство между тенантами не переводилось; physical-device E2E и
браузерная SU-проверка с production credentials не заявляются.
После передачи schema downgrade блокирует потерю владельцев истории; roll forward.
