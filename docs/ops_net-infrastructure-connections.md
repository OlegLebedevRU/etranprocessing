# Инфраструктура, Сетевая топология и Подключение к Базам Данных

Данный документ содержит единый справочник по серверам, базам данных, сетевой топологии и процедурам миграции данных в проекте **etranprocessing** в период перехода с легаси-стека (ASP.NET / MS SQL Server) на современный стек (FastAPI / PostgreSQL / Docker).

---

## 1. Сетевая топология и изоляция контуров

Архитектура разделена на изолированные сетевые контуры:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   Защищенный локальный контур (Legacy)                 │
│                                                                        │
│   • MS SQL Server: 172.17.100.1 (БД: Service, Platerra, Certificates)  │
│   • Legacy IIS Apps: GateGauge, TechGate, licensebilling, BACK         │
│   • Доступен ТОЛЬКО из локальной защищенной сети / рабочего места      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │  (Нет прямого сетевого доступа!)
                                    │  Миграция: через ETL Bridge
                                    ▼  (Скрипты на машине разработчика)
┌────────────────────────────────────────────────────────────────────────┐
│             Единый продуктовый сервер (Primary App Server)             │
│                                                                        │
│   • Хост: 87.242.100.34 (внутренний IP 10.0.0.5, SSH: id_ed25519)     │
│   • Сеть Docker: user1_default                                         │
│   • Бэкенды: processing-backend (:8000), menubuilder-backend (:8000)   │
│   • IoT платформа: app1 (:8000)                                        │
│   • Брокер: rabbitmq (:5672, :8883)                                    │
│   • MCP Сервер: mcp-pin-server (:8001), server-ops                     │
│   • Web & JWT Gateway: nginx-default (:80, :3000, :1443, :1444)        │
│   • Terminal mTLS Gateway: nginx-mutual-legacy (:443)                  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │  (Внутренняя сеть облака 10.0.0.0/24)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│             Кластер Managed PostgreSQL (Yandex Cloud / Bastion)        │
│                                                                        │
│   • Хост: 10.0.0.7 (порт 5432)                                         │
│   • Базы данных: etran (etran_db_user), iot_rpc (leo4_db_user)         │
└────────────────────────────────────────────────────────────────────────┘
```

### Важнейшее сетевое ограничение
> **СЕРВЕРЫ НОВОГО БЭКЕНДА (`87.242.100.34`) НЕ ИМЕЮТ ПРЯМОГО СЕТЕВОГО ДОСТУПА К ЛЕГАСИ MS SQL СЕРВЕРУ (`172.17.100.1`).**
>
> Прямое обращение из Docker-контейнеров нового сервера к легаси-БД невозможно по соображениям сетевой безопасности. Поэтому любой импорт и синхронизация данных (терминалы, лицензии, меню, транзакции) выполняются по схеме **ETL Bridge**:
> 1. **Extract**: Скрипт выполняется в среде, имеющей доступ к защищенной сети (локальная машина разработчика / AI-агента), извлекает данные из MS SQL и формирует валидированный JSON-дамп.
> 2. **Transfer**: JSON-дамп передается на продуктовый сервер через защищенный SSH/SCP (`user1@87.242.100.34`, ключ `d:\.ssh\id_ed25519`).
> 3. **Load**: Модуль импорта (`menubuilder-backend` / `psql`) применяет изменения в PostgreSQL.

---

## 2. Справочник серверов и параметров доступа

### 2.1. Легаси-сервер MS SQL (Legacy Database Server)
* **Хост / IP**: `172.17.100.1` (внутренний адрес в защищенной сети) / `46.38.51.114` (внешний шлюз).
* **СУБД**: Microsoft SQL Server.
* **Порт**: `1433`.
* **Пользователь / Пароль по умолчанию**: `ai-agent` / `ai-agent` (или переменные окружения `MSSQL_USER`, `MSSQL_PASSWORD`).
* **Ключевые базы данных**:
  * **`Service`**:
    * `tb_GS_Group` (`Id`, `org_id`, `number`, `name`, `parentId`) — дерево категорий и групп меню.
    * `tb_GS_Service` (`Id`, `group_id`, `tsp_code`, `name`, `printname`, `price`, `protypenumber`, `action`) — перечень услуг и тарифов.
  * **`Platerra` / `GateGauge` / `Certificates`**:
    * `Kiosks` (`kiosk_id`, `number`, `org_id`, `status`, `license`, `address`) — терминалы и их лицензии.
    * `Certificates` (`cert_id`, `kiosk_id`, `cpserial`, `common_name`, `status_id`, `pin`, `update_datetime`) — X.509 сертификаты и PIN-коды.
    * `Payments` / `tb_Payments` — транзакции платежей.
    * `Inkass` / `tb_Inkass` — данные инкассаций.
* **Метод извлечения данных**: PowerShell `System.Data.SqlClient` или Python-скрипты в защищенной среде (без необходимости установки внешних C/ODBC-драйверов на Windows).

### 2.2. Основной продуктовый сервер (Primary App Server)
* **Хост / IP**: `87.242.100.34` (внутренний IP `10.0.0.5`).
* **Пользователь SSH**: `user1`.
* **SSH-ключ**: `d:\.ssh\id_ed25519`.
* **Команда подключения**: `ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34`.
* **Рабочие директории**:
  * `/home/user1/ProcessingBackend/`
  * `/home/user1/MenuBuilder/`
  * `/home/user1/shared/`
  * `/home/user1/nginx-configs/`
  * `/home/user1/nginx-mutual-legacy/`
* **Docker контейнеры**:
  * `processing-backend`: FastAPI сервис процессинга платежей и меню (порт 8000).
  * `menubuilder-backend`: FastAPI сервис панели управления и биллинга (порт 8000).
  * `nginx-default`: Nginx веб-интерфейс MenuBuilder и JWT API gateway (порты 80, 3000, 1443, 1444).
  * `app1`: IoT platform backend (порт 8000).
  * `rabbitmq`: Очереди сообщений и MQTT (порты 5672, 8883).
  * `mcp-pin-server`: MCP сервер управления PIN-кодами (порт 8001).
  * `nginx-mutual-legacy-nginx-mutual-1`: mTLS терминальный шлюз (порт 443).

### 2.3. Новая база данных (Managed PostgreSQL 18)
* **Хост**: `10.0.0.7:5432`.
* **База данных**: `etran` (для etranprocessing) и `iot_rpc` (для IoT платформы).
* **Пользователи**: `etran_db_user` (БД `etran`), `leo4_db_user` (БД `iot_rpc`).
* **Прямой доступ с сервера**: `psql -h 10.0.0.7 -U etran_db_user -d etran` (пароль из `~/.pgpass`).
* **Ключевые таблицы**:
  * `org_statuses` (`org_id`, `status_id`, `status_name`, `is_blocked`) — организации.
  * `terminals` (`device_id`, `org_id`, `address`, `device_sn`, `status_id`, `cert_not_valid_after`) — терминалы.
  * `licenses` (`id`, `terminal_id`, `license_until`, `is_active`) — лицензии терминалов.
  * `certificate_pins` (`id`, `terminal_id`, `pin`, `status`) — PIN-коды активации.
  * `menu_variants` (`id`, `org_id`, `name`, `created_at`) — варианты меню (например, «Легаси меню»).
  * `groups` (`id`, `menu_variant_id`, `org_id`, `number`, `name`, `parent_id`) — иерархические категории.
  * `services` (`id`, `menu_variant_id`, `group_id`, `tsp_code`, `name`, `printname`, `price`, `protypenumber`, `is_active`) — услуги.
  * `terminal_menu_bindings` (`terminal_id`, `menu_variant_id`) — привязка терминалов к меню.

### 2.4. Внешний mTLS терминальный шлюз (nginx-mutual-legacy)
* **Хост / IP**: `87.242.100.34`.
* **Порт**: `443` (mTLS с проверкой клиентских сертификатов).
* **Маршрутизация**: Внутри Docker-сети `user1_default` передает проверенные запросы терминалов в `http://processing-backend:8000`.

---

## 3. Скрипты и инструменты синхронизации

| Назначение | Скрипт | Описание |
|---|---|---|
| **Синхронизация меню и услуг** | `migrate/sync_menu_from_mssql.py` | Выгрузка `tb_GS_Group` и `tb_GS_Service` из MS SQL, трансформация и импорт в PostgreSQL (поддерживает `--org`, `--all`, `--dry-run`, `--deploy`). |
| **Импорт меню внутри бэкенда** | `MenuBuilder/backend/app/services/legacy_menu.py` | Внутренний сервис MenuBuilder для загрузки JSON-дампа в базу PostgreSQL. |
| **Синхронизация терминалов и PIN** | `migrate/sync_terminals_from_mssql.py` | Выгрузка `Kiosks` и `Certificates` из MS SQL, создание терминалов, лицензий и PIN в PostgreSQL. |
| **Инвентаризация IIS** | `migrate/iis8-inventory.ps1` | Аудит сайтов, пулов, сертификатов и конфигураций на легаси Windows сервере. |

---

## 4. Паттерн написания новых ETL-скриптов

Если в переходный период потребуется импортировать другие данные (например, балансы агентов, историю платежей, реестры ТСП):

1. **Извлечение (Extract) через PowerShell `System.Data.SqlClient`**:
   Позволяет быстро и надежно выполнять SQL-запросы к MS SQL `172.17.100.1` в среде Windows без установки сторонних ODBC драйверов.
2. **Сериализация в JSON**:
   Структурированный JSON сохраняется в директорию `migrate/` и в соответствующий сервис бэкенда.
3. **Идемпотентный загрузчик (Load)**:
   SQLAlchemy 2.0 сервис в `MenuBuilder` или `ProcessingBackend` выполняет транзакционную загрузку данных с очисткой старых записей целевой сущности перед вставкой обновленных.
4. **Развертывание на сервере**:
   Скрипт передает JSON через SCP и запускает выполнение импорта через `sudo docker exec <container> python -m ...`.
