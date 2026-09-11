# Deployment invariants

## Назначение
Ограничения релиза и readiness; не разрешение выполнять деплой из документационной задачи.

## Границы ответственности
Release Agent согласует scope/версии; владельцы producer/consumer подтверждают compatibility;
ProcessingBackend применяет общие Alembic. Runtime host не заменяет локальный source of truth.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Operator → deploy host | SSH/scp | user1@87.242.100.34 | согласованные артефакты | единственный deploy host по умолчанию |
| Release → containers | sudo docker/compose | /home/user1/compose.yaml | версии сервисов | не объявлять успех по одному running |
| Frontend build → nginx-default | live mount | /home/user1/MenuBuilder/frontend/dist | локальный dist | обычно без restart; nginx config — отдельная проверка |
| Git main → beta builder → registry → production | systemd polling / Docker / SSH | builder 176.108.247.249 → 87.242.100.34 | уникальный SHA-build-id и digest | версии кода/worker/launcher, checkpoint только после успешного deploy |

## Инварианты
- SSH из PowerShell всегда `ssh -n`; утверждённый ключ `d:\.ssh\id_ed25519`.
  Не отключать host key verification. Docker/compose на сервере через sudo.
- `176.108.247.249` не использовать без прямого указания пользователя.
- Правки локально → проверки → согласованный релиз; прямые server edits требуют
  отдельного подтверждения и плана backport, не «горячей» правки вне репозитория.
- Общие schema changes: `alembic upgrade head` в processing-backend, затем restart
  menubuilder-backend при изменении shared models; порядок совместимости утвердить заранее.
- Frontend code build локально: `npm --prefix MenuBuilder\frontend run build`.
  Доставлять только согласованные артефакты, не production secrets.
- Для явно согласованного бета-контура действует [бета-регламент](../../docs/ops_run-beta-ci-cd.md):
  `etran-beta.timer` на отдельном builder, frontend как artifact-only image,
  production только pull по digest. GitHub Actions отключён для исключения двойного выпуска.
- Базовый Compose не перезаписывается; штатный up/pull требует image override
  `/home/user1/.etran-ci/user1-images.json` (l4media: `l4media-images.json`).
  Старый up --build без override не использовать для управляемых компонентов.

## State machine
requested → authorized → preflight → validated artifacts → deploy/migrate → verify → handoff;
при ошибке — согласованный rollback или roll-forward, не слепой downgrade данных.

## Проверка
Перед MCP Ops: system_info(load) или service_status, затем RAM >300 MiB,
root disk <90%, load average <2.0; сервер без swap. Зафиксировать один статус:
- `[MCP Ops Readiness: READY]` — transport/resources проверены.
- `[MCP Ops Readiness: DEGRADED]` — ресурсы/доступ ограничены.
- `[MCP Ops Readiness: UNAVAILABLE]` — MCP недоступен.

DEGRADED/UNAVAILABLE не блокирует согласованный деплой: fallback — штатный прямой SSH
на тот же host, с повторной оценкой ресурсов и безопасной последовательностью действий.
MCP state changes с confirmationId подтверждаются явно; secrets — только config_audit,
не чтение raw .env. Для SSH также не выводить raw secret configs.
После релиза проверить endpoint/контракт, revision/container и соответствующий workload.

## Ключевые исходники
- [compose.yaml](../../compose.yaml) — локальная orchestration reference.
- [Alembic](../../ProcessingBackend/backend/alembic/versions) — общие миграции.
- [release handoff](../tasks/release-handoff-template.md) — evidence/compatibility/rollback gates.
- [beta worker](../../.github/ci/beta.py), [установка](../../deploy/beta),
  [бета-регламент](../../docs/ops_run-beta-ci-cd.md) — актуальный автономный flow.

## Известные риски и незавершённые вопросы
2026-09-11 UTC: бета-проверка через SSH, MCP Ops UNAVAILABLE. Builder/production
прошли resource preflight; MenuBuilder выпущен из cd5658c, остальные четыре образа
собраны без деплоя. Timer активен. Это evidence конкретного выпуска, не постоянная
гарантия readiness; при следующем изменении повторять preflight. Retention рабочих
копий/uv cache пока ручной, чужие образы не удалять. Детали digest — в бета-регламенте.

## Источники и актуальность
- Authoritative docs: [runbook](../../docs/ops_run-devops-runbook.md), [AGENTS](../../AGENTS.md),
  текущие пользовательские guidelines (host, sudo, readiness thresholds).
- Проверено: 2026-09-11 UTC, worker `cd5658c`, launcher `52e93a9`; SSH/build/push/pull,
  API health и первый systemd-run подтверждены. Исторический GitHub flow заблокирован billing.
- Обновить при: deploy host, mounts, orchestration, migration/release policy.