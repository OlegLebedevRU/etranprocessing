# Git/release и локальные временные копии — handoff

## Контекст задачи
- Scope: документация GitHub/deploy и проверяемый сценарий очистки локальных копий.
- Репозиторий: `etranprocessing`, от `origin/main` `e08deb9` на 2026-09-30.
- Владелец удаления: оператор запускает подготовленный PowerShell script с manifest.
- Production, broker, terminal и app1 код не изменялись.

## Выполнено
- Разделены обычный Git push в профиле текущего Windows-пользователя и
  разовый обход ошибки транспорта через Git Data API.
- Уточнены условный builder/registry/pull маршрут и отдельный default app1.
- Подготовлен script с dry-run, точными SHA worktree/веток и SHA-256 файлов.
- Выявлены чистые merged worktree и старые архивы/журналы `D:\.codex`.

## Затронутые контракты
| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| Git/release docs | docs и operations card | следующий release agent | порядок уточнён, runtime не менялся |
| Cleanup manifest | локальный аудит | оператор PowerShell | удаление только при совпадении снимка |

## Проверено
- [x] Локальные Git worktree, branch, merge-base и `origin/main` сверены.
- [x] В текущем профиле `gh auth status` и `git fetch origin main` работали.
- [x] `D:\.codex` просмотрен по метаданным; чувствительные файлы не читались.
- [ ] Удаление не запускалось: по запросу пользователя выполняется им отдельно.
- N/A: backend/frontend tests, SSH preflight и deploy — docs/cleanup задача.

## Риски и следующие действия
- Manifest фиксирует состояние на момент создания; любое изменение main или
  выбранных файлов требует нового аудита.
- Грязные и неслитые рабочие деревья, Codex-managed worktree и независимая
  `.scratch-18f` остаются для отдельного разбора.

## Context
- [deployment-invariants](../../operations/deployment-invariants.md) обновлена;
  основной [runbook](../../../docs/ops_run-git-and-release-flow.md) индексирован.
