# GitHub Actions: сборка и точечный production-деплой

## Контракт

`push main → changes → matrix.trigger → tests → build/push SHA → SSH → docker pull digest → health`.
Workflow: `.github/workflows/build-image.yml`. Ручной запуск `workflow_dispatch`:
одна подсистема либо `all`; разрешён только `main`. Pull requests и чужие ветки
не исполняют код на self-hosted runner и не получают deploy-секреты.

| Образ в `dev-leo4-ru.cr.cloud.ru/etran` | Build context | Production |
| --- | --- | --- |
| `menubuilder-backend` | корень | `user1 / menubuilder-backend` |
| `processingbackend` | корень | `user1 / processing-backend` |
| `menubuilder-frontend` | `MenuBuilder/frontend` | извлечение `/dist`, не отдельный сервис |
| `l4media-ingress` | `l4media/ingress` | `l4media / ingress` |
| `l4media-nginx` | `l4media/nginx` | `l4media / nginx` |

Изменения `shared/` выбирают оба Python backend; CI-скрипты, workflow и корневой
`.dockerignore` выбирают все образы. Только Markdown не вызывает выпуск.
Janus, общий Nginx, runtime routes, сертификаты, `.env` и базовый Compose этим
workflow не доставляются. Их изменение требует отдельного согласованного регламента.
В частности, `l4media/ingress/routes.conf` остаётся production bind mount:
изменение копии внутри образа не заменяет конфигурацию маршрутизации на хосте.

## Доступ и границы

- Production: **только `87.242.100.34`, `user1`**, hostname `etranprocessing`
  (публичный адрес через NAT). Все Docker-команды через `sudo -n`.
- Runner: `etran_iot`, labels `self-hosted`, `Linux`, `X64`, пользователь
  `github-runner`. Это отдельный builder, не production.
- Repository secrets: `REGISTRY_KEYID`, `REGISTRY_KEYSECRET`, `DEPLOY_SSH_KEY`,
  `DEPLOY_KNOWN_HOSTS`. Значения в Git и документацию не записывать.
- `DEPLOY_KNOWN_HOSTS` должен происходить из проверенного host key, не из
  непроверенного `ssh-keyscan`. `StrictHostKeyChecking=yes` обязателен.
- Registry credentials на production должны быть доступны именно
  `sudo docker pull`; workflow не пересылает их с runner.
- Deploy-ключ: ED25519, комментарий `github-actions-etranprocessing-deploy`.
  В `authorized_keys` установлен с `restrict`: без PTY, forwarding и user rc.
  Он всё ещё разрешает shell-команды пользователя `user1`, включая его Docker
  sudo-права; это production-привилегия, а не read-only доступ.
- При ротации: подготовить отдельный ключ, согласовать добавление публичной
  строки на production, обновить GitHub Secret, проверить SSH, затем удалить
  только прежнюю согласованную строку. `.ssh` не поставляется приложением.
- Изолированные SSH/Docker credentials создаются в `RUNNER_TEMP`, после job
  удаляются; Buildx post-step выполняется до очистки временной папки раннером.

## Сборка и проверки

Для выбранного Python backend выполняются `uv sync --locked`, компиляция и весь
`uv run --locked pytest`. Frontend Dockerfile выполняет `npm ci` и `npm run build`
(TypeScript + Vite). Ingress Dockerfile выполняет `make test`. Перед матрицей
всегда выполняются тесты CI-контрактов.

`max-parallel: 1` ограничивает загрузку builder. `docker/setup-buildx-action`
кэширует бинарник, `keep-state: true` сохраняет слои BuildKit между job.
Production ничего не компилирует. Публикуется полный Git SHA, без `latest`;
для запуска используется **digest**, полученный непосредственно от Buildx.
Проверяются OCI revision label и ID фактически работающего образа.

CI не делает Python Docker-зависимости воспроизводимыми сам по себе: существующие
backend Dockerfile пока устанавливают диапазоны из `pyproject.toml` через pip,
а не `uv.lock`. Digest фиксирует именно построенный артефакт даже при повторной
сборке того же SHA с обновлёнными upstream-зависимостями.

## Обновление хоста

Базовые `/home/user1/compose.yaml` и `/home/user1/l4media/compose.yaml` не заменяются.
В `/home/user1/.etran-ci/` сохраняются только несекретные image override и записи
выпуска. Команда обновления одного сервиса всегда содержит
`--no-deps --no-build --pull never`: соседние контейнеры не пересоздаются.
Файловый lock защищает от параллельного ручного запуска deploy-скрипта.

Перед pull проверяются RAM > 300 MiB, root usage < 90%, load < 2.0. При нарушении
порогов выпуск останавливается до смены приложения. MCP Ops для этого не нужен.

ProcessingBackend запускает `alembic upgrade head` новым образом до переключения
приложения. **Миграции должны быть обратно совместимы с работающей версией**
(expand/contract). Разрушающие миграции требуют отдельного окна и согласования;
автоматический откат схемы не выполняется. Изменение общих моделей выбирает оба
backend; последовательность матрицы: ProcessingBackend, затем MenuBuilder.

Frontend: остановленный временный контейнер используется только для `docker cp`.
Новые assets копируются до атомарной замены `index.html`; старые assets сохраняются
для уже открытых вкладок и отката. Каталог bind mount не переименовывается.
`nginx-default` не перезапускается. Проверяется ответ Nginx по HTTPS.
Ротация старых assets/образов — отдельная операция после окна отката, не `prune` в CI.

## Выпуск и восстановление

Ручной выпуск из PowerShell:

```powershell
gh workflow run build-image.yml --ref main -f component=menubuilder-backend
gh run list --workflow build-image.yml --limit 5
gh run watch <run-id> --exit-status
```

Проверка и публикация всех образов **без деплоя**:

```powershell
gh workflow run build-image.yml --ref main -f component=all -f deploy=false
```

После health-check ошибка приводит к попытке восстановления предыдущего образа
**только выбранного сервиса**, но workflow остаётся failed. До успешной проверки
постоянный image override не продвигается.

Ручное восстановление последнего MenuBuilder backend (старый образ не удалять):

```powershell
ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker compose -p user1 -f /home/user1/compose.yaml -f /home/user1/.etran-ci/menubuilder-backend-previous.json up -d --no-deps --no-build --pull never menubuilder-backend"
```

После восстановления проверить API и согласовать постоянный image override с
восстановленной версией. Для штатного `up`/`pull` необходимо указывать
`-f /home/user1/.etran-ci/user1-images.json` (для l4media: `l4media-images.json`)
после базового Compose. Старый `docker compose up --build` без override способен
вернуть локальный образ — он больше не является регламентом выпуска этих сервисов.

## Первоначальный опытный выпуск

Однократная repository variable `CI_BOOTSTRAP_COMPONENT=menubuilder-backend`
ограничивает первый push одним сервисом, включая изменения CI/Dockerfile других
компонентов. После успешной проверки **обязательно удалить** переменную:

```powershell
gh variable delete CI_BOOTSTRAP_COMPONENT
```

Ограничение видно в summary запуска. Это не постоянная настройка; значения кроме
пустого и `menubuilder-backend` отвергаются. При сбое ограничения не снимаются
до исправления и проверки опытного выпуска. Невыпущенные компоненты можно затем
выпустить явно через `workflow_dispatch`.

## Локальная проверка CI

```powershell
uv run --no-project --python 3.14 python -m unittest discover -s .github\ci -v
uvx ruff check .github\ci
uvx ruff format --check .github\ci
uvx pyright .github\ci
```