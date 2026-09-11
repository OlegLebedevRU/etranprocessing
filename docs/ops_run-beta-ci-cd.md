# Бета CI/CD: Git → выделенный builder → registry → production

## Статус

Реализация подготовлена в репозитории. Серверная установка, первая публикация
locked-образа и end-to-end выпуск пока не подтверждены. Timer не включён.
До первого успешного выпуска не считать бета-сценарий введённым в эксплуатацию.

## Контракт и версии

- Репозиторий: `https://github.com/OlegLebedevRU/etranprocessing.git`, только `main`.
- Builder: **176.108.247.249**, администрирование `user1`, исполнение `github-runner`.
- Production: **87.242.100.34**, `user1`, Docker через `sudo -n`.
- Registry: `dev-leo4-ru.cr.cloud.ru/etran`.
- Оркестрация: свой `systemd timer`, без GitHub Actions/API/биллинга CI.
- Ветка проверяется через 60 секунд после окончания предыдущей проверки; это
  polling после push, а не входящий webhook. Публичный listener не создаётся.
- Worker и deploy-скрипт исполняются из checkout точного commit main. Установленный
  launcher — отдельная минимальная часть, его SHA записывается в `/opt/etran-beta/installed-revision`.
- Образ получает уникальный тег `<полный-Git-SHA>-<UTC-build-id>`. Тег `latest`
  не используется; тег конкретной попытки не перезаписывается.
- JSON артефакта связывает компонент, SHA кода/worker/установленного launcher,
  тег, digest и время сборки.
  Деплой принимает **digest**; label revision и ID работающего образа проверяются.
- При повторе того же SHA используется ранее опубликованный digest, а не новая
  сборка. Если registry push завершился, но ответ/локальная запись потерялись,
  повторная сборка получает новый build-id, не перетирая предыдущий тег.

Python runtime-зависимости устанавливаются через `uv sync --locked --no-dev --no-editable`.
`uv.lock` обоих backend и `package-lock.json` frontend входят в Git. Frontend
Dockerfile выполняет `npm ci`, `npm test` и production build; `vitest` явно
зафиксирован в devDependencies, а не берётся из остаточного node_modules.
Базовые образы зафиксированы linux/amd64 digest. Buildx 0.37.1 и uv 0.12.5
скачиваются с проверкой SHA256 из `deploy/beta/versions.json`.
Это предсказуемая идентичность выпуска, не обещание побайтово одинаковой
пересборки: build-system requirements Python и пакеты apk ingress пока не
зафиксированы отдельным snapshot package repository.

## Выбор компонентов и повтор после ошибки

Переиспользуется `.github/ci/components.py`: пять образов, `shared/` выбирает оба
backend; `.github/ci`, `deploy/beta` и корневой `.dockerignore` затрагивают все.
Только Markdown не вызывает выпуск. Выпуски последовательные: ProcessingBackend
перед MenuBuilder. Ошибка останавливает текущий цикл и повторяется timer позднее.

База сравнения — последний **успешно развёрнутый** SHA каждого компонента.
Checkpoint изменяется только после успешного удалённого деплоя; build-only его
не меняет. Ошибка не теряет накопившиеся commits. Совпадающий SHA не запускается снова.

`--initialize` один раз фиксирует bootstrap SHA без фиктивного деплоя:
до первого управляемого выпуска остальные компоненты сравниваются с ним.
Это граница начала наблюдения, **не утверждение**, что старые production-образы
собраны из этого commit. Первый MenuBuilder выпускается явно. Остальные затем
выбираются изменениями либо ручным запуском.

`builder.lock` защищает timer и ручной запуск одним lock. Production имеет
дополнительный lock. Состояние записывается атомарно с fsync.

## Файлы установки для согласования

Все кодовые файлы ниже поставляются из одного reviewed commit, без ручных
серверных правок исходников. Установщик `deploy/beta/install.py` не включает timer.

| Builder: серверный путь | Источник/назначение |
| --- | --- |
| `/opt/etran-beta/launcher.py` | `deploy/beta/launcher.py`, root-owned 644 |
| `/opt/etran-beta/versions.json` | версии инструментов из того же commit |
| `/opt/etran-beta/installed-revision` | полный SHA установленного launcher |
| `/etc/systemd/system/etran-beta.service` | точная копия `deploy/beta/etran-beta.service` |
| `/etc/systemd/system/etran-beta.timer` | точная копия `deploy/beta/etran-beta.timer` |
| `/home/github-runner/.docker/cli-plugins/docker-buildx` | проверенный бинарник Buildx |
| `/home/github-runner/.local/bin/uv` | проверенный бинарник uv |
| `/home/github-runner/etran-ci/keys/deploy` | отдельный закрытый ключ, 600; НЕ в Git |
| `/home/github-runner/etran-ci/keys/known_hosts` | ранее проверенный ED25519 host key production |
| `/home/github-runner/etran-ci/` | служебные Git worktree, состояния, артефакты и кэш, 700 |

Стандартные настройки службы: `User=github-runner`, `Group=github-runner`,
`UMask=0077`, `TimeoutStartSec=3600`, запуск `/usr/bin/python3 /opt/etran-beta/launcher.py`.
Timer: `OnBootSec=60s`, `OnUnitInactiveSec=60s`, `AccuracySec=5s`.
Полный точный текст unit-файлов хранится в `deploy/beta/`.

Production: добавить **только одну строку**, сохранив все прежние ключи,
в `/home/user1/.ssh/authorized_keys`:

```text
restrict ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFHkqt27oxPDykc9f79h7r2VtWHDWQZUtGk2qXK8DtsD etran-beta-deploy
```

Это разовая настройка доступа, не файл образа. `restrict` запрещает forwarding
и PTY, но разрешает выполнение команд пользователя и его Docker sudo-права.
Старый ключ GitHub не удаляется автоматически; прежний workflow выключен
условием `changes.if: false`, поэтому не возобновит выпуск при исправлении billing.
При последующих деплоях `.ssh`, systemd и runtime `.env` не перезаписываются.

Закрытый ключ подготовлен локально в `.beta-ci-secrets/` (Git ignore, ограниченный
ACL) и после доставки должен быть удалён вместе с временной серверной копией.
Секреты не включать в tar исходников или git add; `git add .` для bootstrap запрещён.
Временный установочный каталог сервера должен быть 700, файлы ключей — 600.

## Порядок доставки после подтверждения

1. Локальные тесты и проверка diff; commit/push согласованных файлов (без секретов).
2. Из **этого SHA** подготовить архив `deploy/beta` и доставить через scp на builder
   в закрытый `/home/user1/etran-beta-bootstrap/`. Передать ключ отдельно по SSH.
3. Выполнить на builder из этого каталога:

```sh
sudo python3 deploy/beta/install.py --revision <SHA> --deploy-key deploy --known-hosts known_hosts
```

4. На production доставить только `authorize_key.py` и публичный `deploy.pub`,
   выполнить под `user1`: `python3 authorize_key.py deploy.pub`. Удалить временные
   установочные файлы, созданные этой операцией, без затрагивания старых файлов.
5. Проверить SSH builder → production с отдельными `IdentityFile` и
   `UserKnownHostsFile`, обязательно `BatchMode=yes`, `StrictHostKeyChecking=yes`.
6. Инициализировать наблюдение и провести первый выпуск:

```powershell
ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo -n -u github-runner -H python3 /opt/etran-beta/launcher.py --initialize"
ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo -n -u github-runner -H python3 /opt/etran-beta/launcher.py --component menubuilder-backend"
```

7. Проверить registry digest, health API, revision контейнера и неизменность ID
   соседних сервисов. Только затем `sudo systemctl enable --now etran-beta.timer`.

Приватный Git доступ при необходимости задаётся отдельно read-only deploy key;
при preflight репозиторий успешно читался без GitHub Actions. Registry credentials
должны быть доступны `docker` пользователя `github-runner` на builder и `sudo docker`
на production. Их наличие проверяется фактическим push/pull, не чтением секретов.
Если авторизация отсутствует, остановиться и запросить настройку у владельца.

## Эксплуатация

```powershell
# Только сборка/публикация без деплоя
ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo -n -u github-runner -H python3 /opt/etran-beta/launcher.py --component menubuilder-frontend --build-only"
# Последние логи и состояние таймера
ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo journalctl -u etran-beta.service -n 100 --no-pager; systemctl status etran-beta.timer --no-pager"
# Приостановить будущие автоматические выпуски (не прерывает текущий)
ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "sudo systemctl disable --now etran-beta.timer"
```

В builder `state.json` хранит успешные releases, `artifacts/<component>/<SHA>.json`
— опубликованные образы, `releases.jsonl` — журнал успешных деплоев. Ручной вывод
сценария идёт в терминал, автоматический — в journald.

Установленные launcher/service/версии инструментов обновляются отдельной
согласованной установкой из нового commit. Worker всегда берётся из текущего
main; нельзя менять его непосредственно на сервере. Иные уже существующие
версии инструментов установщик не перезаписывает молча.

Builder требует >=800 MiB свободной RAM, >=2 GiB диска и usage <90%; BuildKit
ограничен 1536 MiB, свой кэш — 1 GiB. Проверки относятся к отдельному namespace
`etran-beta`; существующие образы/контейнеры не удаляются через общий prune.
Git worktree, артефакты и uv cache пока требуют плановой ручной retention-политики;
при заполнении диска pipeline остановится, а не удалит чужие данные.

Production-деплой переиспользует `.github/ci/deploy.py`: pull по digest, image
override вместо замены базового Compose, `--no-deps --no-build --pull never`,
health-check и попытка отката образа при ошибке. Автоматический downgrade БД
запрещён; миграции ProcessingBackend должны быть обратно совместимыми.
Frontend обновляет assets до `index.html`, сохраняет старые assets и не
перезапускает общий Nginx. Runtime routes l4media, Janus, `.env` и сертификаты
автоматически не доставляются.

Ручной откат образа и правила постоянного Compose override описаны в
[предыдущем регламенте](ops_run-github-actions-ci-cd.md#выпуск-и-восстановление).
Перед ручным откатом остановить timer; после него согласовать checkpoint builder
с выбранной версией, чтобы автоматизация не вернула нежелательный выпуск.