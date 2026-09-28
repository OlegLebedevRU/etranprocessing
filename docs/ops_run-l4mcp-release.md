# L4mcp v1: штатный выпуск без потери серверного Compose

## Основание

Источник выпуска — commit `8633668` ветки `feat/l4mcp-v1` и последующие проверенные коммиты этой ветки. Менять исходники app1 и БД не требуется. На production код внутри image `menubuilder-backend` совпадает с базовым commit `a361086`; файлы `/home/user1/MenuBuilder/backend/app` на хосте старее image и не должны использоваться для сборки. Серверный `/home/user1/compose.yaml` содержит рабочий Redis и приватные значения Nginx inline; полная замена его локальным файлом недопустима.

## Адресный состав выпуска

1. Сохранить прежний `menubuilder-backend` image ID, текущий `mcp-pin-server` image ID и копии `/home/user1/compose.yaml`, `/home/user1/nginx-configs/port_3000.conf`, `/home/user1/MenuBuilder/backend/.env` в приватном release каталоге с правами 600 для env. Секреты не выводить в лог.
2. Создать release каталог из проверенного Git archive; сверить SHA-256 архива. Синхронизировать с ним изменённые исходники MenuBuilder backend и каталог `MenuBuilder/l4mcp`. Frontend dist собрать из того же commit и доставить отдельным артефактом со своим SHA-256. Строить backend и L4mcp только из архива, не из устаревших исходников хоста.
3. В приватный `/home/user1/MenuBuilder/backend/.env` добавить `L4MCP_URL` и `MENUBUILDER_API_URL` с внутренними Docker адресами, права 600. Другие значения не менять.
4. В серверном Compose заменить **только** блок сервиса `mcp-pin-server` (строки 83–89 на момент аудита) на блок `l4mcp` с тем же приватным env-файлом; оставить Redis, nginx, порты, volumes и существующие настройки других сервисов без изменений. Локальный `compose.yaml` дополнен работающим Redis как источником будущих выпусков; существующие секреты Nginx на сервере требуют отдельного безопасного переноса в приватный env до полной замены Compose.
5. Изменить `/home/user1/nginx-configs/port_3000.conf` только для `/api/mcp/`: передать Authorization в MenuBuilder backend и отключить старую проверку `X-Auth-Jti` в этом location. Проверить `nginx -t`, затем reload. До переключения проверить `/health` L4mcp и backend `/docs`.
6. Пересоздать только `l4mcp`, `menubuilder-backend`; после проверки нового маршрута остановить старый `mcp-pin-server`. Frontend dist подхватывается bind mount без рестарта nginx.

## Приёмка и откат

Проверить роли 1/3/5 и отказ 2/4, отзыв токена, точечный preflight для `sys`, offline и `svc_online=false`, одну короткую команду на тестовом терминале, освобождение lease и существующий console usage. Выдачу PIN проверить не более чем для пяти заранее выбранных тестовых терминалов, без массового запроса. При отказе вернуть прежний backend image, Compose и nginx-конфиг из копий, перезапустить прежний MCP-контейнер. После успешного выпуска удалить временный архив и тестовые токены.
