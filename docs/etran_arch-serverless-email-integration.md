# Интеграция Serverless-сервиса отправки Email в экосистему etranprocessing

Документ описывает архитектуру, спецификацию и пошаговый план интеграции внешнего serverless-сервиса отправки электронной почты (Yandex Cloud API Gateway + Cloud Function + Yandex Postbox + Object Storage) в платформу `etranprocessing` для обеспечения четырех ключевых бизнес-сценариев:
1. **Email организации** (официальные контакты тенантов, уведомления о балансе и лицензиях).
2. **Подтверждение email** (двухэтапная верификация контактных адресов, токены и защита от злоупотреблений).
3. **Отправка отчетов** (финансовые реестры, Z-отчеты смен, акты инкассации и выгрузки с вложениями).
4. **Проброс API -> Email Sender от терминалов** (существующий защищенный шлюз mTLS Nginx :1443/:1444 с извлечением `device_id` из клиентских сертификатов).

---

## 1. Архитектура и спецификация Serverless-сервиса

### 1.1. Инфраструктурный стек сервиса
Сервис развернут в инфраструктуре Yandex Cloud и состоит из четырех взаимосвязанных компонентов:

```
[ Терминал (mTLS) ] ──> Nginx (:1443/:1444) ──┐
                                              │
[ MenuBuilder Backend ] ──────────────────────┼──> Yandex API Gateway
                                              │    (d5dbnvm0kd5ames2tb0o)
[ ProcessingBackend ] ────────────────────────┘              │
                                                             ▼
                                                   Yandex Cloud Function
                                                   (d4esceg1niqljs8jcle9)
                                                     │               │
                            (Сохранение вложений)    │               │ (Отправка писем SESv2)
                                                     ▼               ▼
                                            Yandex Object Storage    Yandex Postbox
                                            (/function/storage/      (postbox.cloud.yandex.net)
                                             terem-files/{id}/)              │
                                                                             ▼
                                                                     SMTP Получатели
                                                                     (o.lebedev@platerra.ru)
```

1. **Yandex API Gateway**:
   - URL: `https://d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net`
   - Маршрут: `/backend-api/v1/send-email/{device_id}`
   - Метод: `POST` (также поддерживается `OPTIONS` для CORS preflight с `max-age: 86400`).
   - Идентификатор шлюза: `d5dbnvm0kd5ames2tb0o`.
2. **Yandex Cloud Function**:
   - ID функции: `d4esceg1niqljs8jcle9` (тег `$latest`).
   - Сервисный аккаунт: `ajesdqhvl4494m7ca5jq`.
   - Исполняемая среда: Python 3.12+ / Boto3 / SESv2.
3. **Хранилище файлов (Object Storage / S3 Bucket)**:
   - Точка монтирования в функции: `/function/storage/terem-files`.
   - Путь сохранения: `/function/storage/terem-files/{device_id}/{file_name}`.
   - Сервис сохраняет входящий Base64-файл в смонтированный бакет, повторно вычитывает его из хранилища и прикрепляет к письму, обеспечивая долговременный архив отправленных файлов и отчетов.
4. **Почтовый транспорт (Yandex Postbox)**:
   - Endpoint: `https://postbox.cloud.yandex.net` (AWS SESv2-совместимый протокол).
   - Регион: `ru-central1`.
   - Аутентификация: `ACCESS_KEY_ID` / `SECRET_ACCESS_KEY` через переменные окружения функции.

---

### 1.2. Спецификация контракта API (OpenAPI)

#### Запрос: `POST /backend-api/v1/send-email/{device_id}`
- **Path-параметры**:
  - `device_id` (`string`, 1..128 символов): Идентификатор устройства или системного модуля. Санитизируется регулярным выражением (допускаются символы `[A-Za-z0-9._-]`).
- **Заголовки**:
  - `Content-Type: application/json` (строго обязательно).
- **Тело запроса (JSON)**:
```json
{
  "recipients": ["o.lebedev@platerra.ru"],
  "subject": "Тема сообщения",
  "message": "Текст сообщения (поддерживаются переводы строк)",
  "file_name": "report_20260907.pdf",
  "file_base64": "<base64_encoded_binary_data>"
}
```

Поля тела:
- `recipients` (`array[string]`, minItems 1, обязательное): Список адресов электронной почты. Дубликаты автоматически исключаются без учета регистра.
- `subject` (`string`, опционально): Тема письма. При отсутствии формируется автотема: `"Файл от устройства {device_id}: {file_name}"` или `"Сообщение от устройства {device_id}"`.
- `message` (`string`, опционально): Текст сообщения. Автоматически преобразуется как в plain text, так и в HTML (`<p>...<br>...</p>`) с экранированием HTML-сущностей.
- `file_name` (`string`, 1..255 символов, опционально): Имя файла вложения. Если `file_base64` передан, а `file_name` опущен, генерируется: `file-{device_id}-{timestamp}.txt`.
- `file_base64` (`string`, опционально): Бинарное содержимое вложения в Base64. Максимальный размер: 10 МБ (`SEND_EMAIL_MAX_FILE_BYTES`).

#### Ответ при успехе (HTTP 200 OK):
```json
{
  "status": "sent",
  "device_id": "test-device-001",
  "recipients": ["o.lebedev@platerra.ru"],
  "subject": "Тема сообщения",
  "postbox_message_id": "DL8Y3SO3IGAU.2FWKOJ6D62XFK@ingress1-klg",
  "file_name": "report_20260907.pdf",
  "storage_path": "/function/storage/terem-files/test-device-001/report_20260907.pdf"
}
```

#### Ответы при ошибках:
- **HTTP 400 Bad Request**:
  `{"error_code": "VALIDATION_ERROR", "message": "Field 'recipients' must contain valid email addresses."}`
- **HTTP 405 Method Not Allowed**:
  `GET no allowed on this server` (разрешены только `POST` и `OPTIONS`).
- **HTTP 503 Service Unavailable**:
  `{"error_code": "EMAIL_SEND_FAILED", "message": "Unable to save file or send email: ..."}` (сбой конфигурации Postbox или доступа к бакету).

---

## 2. Результаты верификации и практического тестирования

В ходе верификации сервиса выполнен комплекс практических тестов через публичный API Gateway с отправкой на контрольный адрес `o.lebedev@platerra.ru`:

| Тестовый сценарий | Метод / Входные параметры | Ожидаемый результат | Фактический результат | Статус |
|---|---|---|---|---|
| **1. Отправка без вложений** | `POST /backend-api/v1/send-email/test-device-001`, `recipients: ["o.lebedev@platerra.ru"]` | HTTP 200, статус `sent`, генерация `postbox_message_id` | HTTP 200 OK, `postbox_message_id`: `DL8Y1ZX6V3JF.3OWP057KK70MZ@ingress1-vla` | **Успешно** |
| **2. Отправка с вложением** | `POST ...`, `file_name: "shift_report_20260907.txt"`, `file_base64` (отчет смены) | HTTP 200, запись в S3, доставка письма с вложением | HTTP 200 OK, `postbox_message_id`: `DL8Y3SO3IGAU.2FWKOJ6D62XFK@ingress1-klg`, `storage_path`: `/function/storage/terem-files/test-device-001/shift_report_20260907.txt` | **Успешно** |
| **3. Валидация некорректного email** | `recipients: ["not-an-email"]` | HTTP 400 `VALIDATION_ERROR` | HTTP 400, `message`: `Field 'recipients' must contain valid email addresses.` | **Успешно** |
| **4. Валидация пустого списка** | `recipients: []` | HTTP 400 `VALIDATION_ERROR` | HTTP 400, `message`: `Field 'recipients' must contain at least one email address.` | **Успешно** |
| **5. Проверка метода GET** | `GET /backend-api/v1/send-email/test-device-001` | HTTP 405 Method Not Allowed | HTTP 405, `GET no allowed on this server` | **Успешно** |
| **6. CORS Preflight (OPTIONS)** | `OPTIONS /backend-api/v1/send-email/test-device-001` | HTTP 200, CORS заголовки | HTTP 200 OK, `Access-Control-Allow-Origin: *`, `Max-Age: 86400` | **Успешно** |

> **Ключевой вывод проверки:** Серверлесс-сервис, функция и шлюз Yandex API Gateway полностью исправны, прикрепление файлов через монтирование S3-бакета работает штатно, письма гарантированно доставляются через Yandex Postbox.

---

## 3. Цель 1: Интеграция Email организации

### 3.1. Контекст в доменной модели
В общей схеме данных `shared/etranprocessing_db` (модель `Org`, таблица `orgs`) уже предусмотрены необходимые реквизиты:
- **`email`** (`VARCHAR(255)`, nullable): Официальный контактный e-mail организации/тенанта.
- **`notify_by_email`** (`BOOLEAN`, default `True`): Пользовательский флаг согласия на получение уведомлений.

В `MenuBuilder/backend` роутер `admin_organizations.py` предоставляет методы просмотра и редактирования данных полей:
- `GET /api/admin/organizations`
- `PATCH /api/admin/organizations/{org_id}` (поля `email`, `notify_by_email`)

### 3.2. События и типы уведомлений
1. **Биллинговые уведомления (Лицензии терминалов)**:
   - **Приближение срока оплаты (`DUE_SOON`)**: Отправка предупреждения за 5 и 3 дня до даты якоря (`anchor_day`).
   - **Образование задолженности (`OVERDUE`)**: Уведомление о переводе терминалов в режим задолженности с детализацией суммы к оплате.
   - **Блокировка терминалов (`DISABLED`)**: Экстренное уведомление о приостановке процессинга терминалов при неоплате лицензий.
   - **Квитанция об успешном платеже**: Отправка акта/справки о списании средств и продлении срока лицензий.
2. **Финансовые уведомления (Баланс организации)**:
   - **Low Balance Alert**: Предупреждение о снижении операционного баланса ниже установленного порога (например, < 10 000 руб.).
   - **Пополнение баланса**: Подтверждение зачисления денежных средств.
3. **Технический мониторинг парка**:
   - Массовый переход терминалов в статус `Offline` (> 30 минут).
   - Инциденты с аппаратным оборудованием (переполнение купюроприемника, окончание чековой ленты).

### 3.3. Реализация клиента отправки в бэкенде (`EmailService`)
Для работы из Python-сервисов (`MenuBuilder` и `ProcessingBackend`) создается единый асинхронный клиент:

```python
# app/services/email_client.py
import httpx
import logging
from typing import Sequence

logger = logging.getLogger(__name__)

class ServerlessEmailClient:
    def __init__(self, gateway_url: str = "https://d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net"):
        self.gateway_url = gateway_url.rstrip("/")
        self.timeout = httpx.Timeout(15.0, connect=5.0)

    async def send_email(
        self,
        device_id: str,
        recipients: Sequence[str],
        subject: str,
        message: str,
        file_name: str | None = None,
        file_bytes: bytes | None = None,
    ) -> dict:
        import base64
        payload = {
            "recipients": list(recipients),
            "subject": subject,
            "message": message,
        }
        if file_bytes and file_name:
            payload["file_name"] = file_name
            payload["file_base64"] = base64.b64encode(file_bytes).decode("ascii")

        url = f"{self.gateway_url}/backend-api/v1/send-email/{device_id}"
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(url, json=payload)
            resp.raise_for_status()
            return resp.json()
```

---

## 4. Цель 2: Подтверждение Email (Email Verification Workflow)

### 4.1. Архитектурная схема верификации
Процесс подтверждения гарантирует, что владелец организации указал корректный и доступный почтовый ящик:

```
[Пользователь] ──(1) Ввод/изменение email──> [MenuBuilder API]
                                                    │
                                         (2) Генерация токена & OTP
                                         (3) Запись в БД (status: pending)
                                                    │
                                         (4) POST API Gateway
                                                    │
[Почтовый ящик] <──(5) Письмо со ссылкой и OTP ─────┘
      │
(6) Клик по ссылке / Ввод OTP
      │
      ▼
[MenuBuilder Frontend] ──(7) POST /api/auth/email/verify──> [MenuBuilder Backend]
                                                                   │
                                                      (8) Проверка токена
                                                      (9) is_email_verified = True
```

### 4.2. Изменения в схеме БД (`shared/etranprocessing_db`)
В модель `Org` (и опционально `User`) добавляются флаги верификации, а также вводится специализированная таблица аудита токенов `email_verifications`:

```sql
-- Добавление статуса верификации в таблицу orgs
ALTER TABLE orgs ADD COLUMN is_email_verified BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE orgs ADD COLUMN email_verified_at TIMESTAMP WITH TIME ZONE NULL;

-- Таблица отслеживания токенов верификации
CREATE TABLE email_verifications (
    id SERIAL PRIMARY KEY,
    org_id INTEGER NOT NULL REFERENCES orgs(org_id) ON DELETE CASCADE,
    email VARCHAR(255) NOT NULL,
    token_hash VARCHAR(64) NOT NULL UNIQUE,
    otp_code VARCHAR(6) NOT NULL,
    attempts_left INTEGER NOT NULL DEFAULT 5,
    expires_at TIMESTAMP WITH TIME ZONE NOT NULL,
    is_used BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_email_verif_token ON email_verifications(token_hash);
CREATE INDEX idx_email_verif_org ON email_verifications(org_id);
```

### 4.3. API-эндпоинты верификации
1. **Запрос на отправку письма с подтверждением**:
   - `POST /api/admin/organizations/{org_id}/request-email-verification`
   - Логика:
     - Проверка прав суперпользователя или администратора данной организации.
     - Проверка Cooldown (не чаще 1 раза в 60 секунд на организацию).
     - Генерация криптографически стойкого URL-токена (`secrets.token_urlsafe(32)`) и 6-значного числового кода (`secrets.randbelow(900000) + 100000`).
     - Хэширование токена (SHA-256) и сохранение в `email_verifications` со сроком жизни 24 часа.
     - Вызов serverless email sender с `device_id="sys-verify-{org_id}"`.
2. **Подтверждение по токену (из ссылки)**:
   - `GET /api/auth/verify-email?token={raw_token}` или `POST /api/auth/verify-email`
   - Логика:
     - Вычисление SHA-256 от `raw_token`, поиск активной записи в `email_verifications`.
     - Проверка `expires_at > now()` и `is_used == False`.
     - Обновление: `is_used = True`, в `orgs`: `is_email_verified = True`, `email_verified_at = now()`.
3. **Подтверждение по OTP-коду (ручной ввод в UI)**:
   - `POST /api/admin/organizations/{org_id}/confirm-email-otp`
   - Тело: `{"code": "123456"}`.
   - Декремент `attempts_left`. При достижении 0 запись аннулируется.

---

## 5. Цель 3: Отправка отчетов (Shift Reports, Z-Reports, Inkass)

### 5.1. Формирование и доставка отчетов
Платформа etranprocessing генерирует несколько критически важных классов отчетов:

| Тип отчета | Источник данных | Формат | Триггер формирования | Получатели |
|---|---|---|---|---|
| **Отчет инкассации** | `tech_gate.py` (`_process_inkass_at_receipt`) | Текстовый чек / PDF | Выполнение инкассации на терминале | Финотдел организации, инкассаторская служба |
| **Z-отчет закрытия смены** | `tech_gate.py` (`closeshift`, `closeshift2`) | Текстовый / CSV | Закрытие операционного дня терминалом | Бухгалтерия тенанта, системный архив |
| **Реестр платежей за период** | `ProcessingBackend` (таблица `payments`) | CSV / Excel (.xlsx) | По расписанию (cron) или по запросу из UI | Владелец организации (`org.email`) |
| **Акт сверки лицензий** | `MenuBuilder` (биллинг лицензий) | PDF / CSV | Ежемесячно 1-го числа | Руководство организации |

### 5.2. Асинхронная схема генерации и отправки
Так как формирование тяжелых отчетов и обращение к внешнему API Gateway занимает время, отправка выносится в фоновые задачи (`BackgroundTasks` или воркер очереди):

```python
# Пример интеграции в роут закрытия смены / инкассации
from fastapi import BackgroundTasks

async def async_send_shift_report(
    terminal_sn: str,
    device_id: str,
    org_email: str,
    report_text: str,
    report_pdf_bytes: bytes,
):
    client = ServerlessEmailClient()
    file_name = f"shift_{terminal_sn}_{datetime.now().strftime('%Y%m%d_%H%M')}.pdf"
    
    await client.send_email(
        device_id=device_id,
        recipients=[org_email],
        subject=f"Отчет закрытия смены терминала {terminal_sn}",
        message=f"Здравствуйте.\nВо вложении отчет закрытия смены терминала {terminal_sn}.\n\nСводка:\n{report_text}",
        file_name=file_name,
        file_bytes=report_pdf_bytes,
    )

@router.post("/closeshift")
async def closeshift(
    request: Request,
    background_tasks: BackgroundTasks,
    terminal: Terminal = Depends(get_current_terminal),
    db: AsyncSession = Depends(get_db),
):
    # ... обработка закрытия смены в БД ...
    org = await get_terminal_org(db, terminal.org_id)
    if org and org.email and org.notify_by_email:
        background_tasks.add_task(
            async_send_shift_report,
            terminal.sn,
            terminal.device_id,
            org.email,
            shift_summary,
            pdf_bytes,
        )
    return xml_response("<Response><Result>OK</Result></Response>")
```

### 5.3. Архивирование в Object Storage
Особенностью используемой серверлесс-функции является автоматическое сохранение файлов в S3-бакет по пути:
`/function/storage/terem-files/{device_id}/{file_name}`
Это предоставляет встроенный аудит: любой отправленный финансовый или сменный отчет навсегда сохраняется в облачном объектном хранилище с привязкой к конкретному `device_id` терминала.

---

## 6. Цель 4: Проброс API -> Email Sender от терминалов (Существующий контур mTLS)

### 6.1. Архитектура шлюза Nginx mTLS (:1443 / :1444)
Для терминалов самообслуживания в проекте развернут выделенный защищенный mTLS-прокси (`nginx-configs/terem_email_mtls.conf` и `nginx-configs/terem_email_proxy.inc`):

```
 Терминал (Kiosk)
   │ HTTPS POST :1443/:1444 (mTLS)
   │ Client Certificate (OU={device_id})
   ▼
 Nginx Reverse Proxy (dev.leo4.ru)
   │ 1. Валидация сертификата через /crt/ca_certificate.pem
   │ 2. Извлечение $terem_device_id из $ssl_client_s_dn (поле OU)
   │ 3. Если OU отсутствует -> 403 Forbidden
   │ 4. Форвардинг на upstream
   ▼
 Yandex API Gateway (:443)
   (d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net/backend-api/v1/send-email/$terem_device_id)
   ▼
 Yandex Cloud Function (d4esceg1niqljs8jcle9)
```

### 6.2. Сетевые порты и криптографические профили
1. **Порт `1443` (Strict Modern mTLS)**:
   - Протоколы: `TLSv1.2 TLSv1.3`.
   - Назначение: Терминалы с современными сетевыми библиотеками (OpenSSL, cURL, .NET 6+).
2. **Порт `1444` (Windows Schannel Compatibility Profile)**:
   - Протокол: строго `TLSv1.2`.
   - Назначение: Легаси терминальные клиенты под Windows, использующие нативный стек Schannel / WinHTTP.
   - Особенности:
     - Расширенный набор RSA шифров (включая AES-GCM и fallback AES-CBC):
       `ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:@SECLEVEL=1`.
     - Фиксация стандартных кривых NIST: `prime256v1:secp384r1`.
     - Ограничение алгоритмов подписи классическими RSA PKCS#1 SHA (`RSA+SHA256:RSA+SHA384:RSA+SHA512`) без RSA-PSS, что устраняет ошибку Windows `SEC_E_ALGORITHM_MISMATCH`.

### 6.3. Конфигурация проксирования (Source of Truth)

Фрагмент из `nginx-configs/terem_email_mtls.conf`:
```nginx
map $ssl_client_s_dn $terem_device_id {
    default "";
    ~(^|,)\s*OU=([^,/]+)(,|$) $2;
    ~(^|/)OU=([^/]+)(/|$) $2;
}

upstream terem_email_gateway {
    server d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net:443;
    zone terem_email_gateway_zone 64k;
    keepalive 16;
}
```

Фрагмент из `nginx-configs/terem_email_proxy.inc`:
```nginx
if ($terem_device_id = "") {
    return 403 "Forbidden: client certificate OU is required\n";
}

proxy_pass https://terem_email_gateway/backend-api/v1/send-email/$terem_device_id$is_args$args;
proxy_http_version 1.1;
proxy_set_header Host d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net;
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
proxy_set_header X-Forwarded-Proto https;
proxy_set_header X-SSL-Client-Verify $ssl_client_verify;
proxy_set_header X-SSL-Client-Subject $ssl_client_s_dn;
proxy_set_header X-Device-Id $terem_device_id;
proxy_ssl_server_name on;
proxy_ssl_name d5dbnvm0kd5ames2tb0o.apigw.yandexcloud.net;
proxy_request_buffering off;
proxy_buffering off;
```

### 6.4. Форматы полезной нагрузки от терминалов
Существуют два сценария отправки от терминалов:

#### Сценарий А (Штатный / Рекомендуемый): Терминал отправляет JSON c Base64
Терминал отправляет запрос на `https://dev.leo4.ru:1443/terem-api/v1/send-email` (или порт `:1444`):
- **Headers**:
  `Content-Type: application/json`
- **Body**:
```json
{
  "recipients": ["support@platerra.ru"],
  "subject": "Аварийный лог терминала 1702",
  "message": "Сбой купюроприемника CashCode SM в 11:42",
  "file_name": "PlaterraTerminal_1702.log",
  "file_base64": "<base64_log_content>"
}
```
Nginx прозрачно передает данный запрос на Yandex API Gateway, подставляя `{device_id}` из сертификата. Серверлесс-функция принимает его без изменений.

#### Сценарий Б (Legacy Fallback): Терминал отправляет бинарный файл с query-параметрами
Если легаси ПО терминала отправляет `POST /terem-api/v1/send-email?file_name=PlaterraTerminal.log&email_address=user@example.com` с бинарным телом (raw bytes), текущая версия `serverless_email_sender.py` вернет HTTP 400 (`Content-Type must be application/json`).
Для полной поддержки такого легаси-режима рекомендуется добавить в `serverless_email_sender.py` альтернативную ветку парсинга:
```python
# Если Content-Type не application/json, но передан query-параметр email_address
query_email = get_query_param(event, "email_address")
if query_email:
    recipients = [query_email]
    file_name = get_query_param(event, "file_name") or f"file-{device_id}.bin"
    file_bytes = base64.b64decode(event["body"]) if event.get("isBase64Encoded") else event["body"].encode("latin1")
```

---

## 7. Безопасность, ограничения и надежность

1. **Защита от несанкционированного доступа**:
   - Терминальный контур: Доступ открыт только при наличии валидного клиентского сертификата, подписанного доверенным удостоверяющим центром (`ca_certificate.pem`). Отсутствие `OU` блокируется Nginx с кодом 403.
   - Административный контур (`MenuBuilder`): Вызовы API инициируются авторизованными пользователями с проверкой JWT (`RS256`).
2. **Ограничения по размеру (Rate & Size Limits)**:
   - Максимальный размер вложения: **10 МБ** в функции (`SEND_EMAIL_MAX_FILE_BYTES`) и **25 МБ** в Nginx (`client_max_body_size 25m`).
   - Таймаут ожидания API Gateway / Nginx: 15 секунд.
   - Санитизация имен файлов и идентификаторов: Исключение path-traversal (`..`, `/`, `\`) регулярным выражением `[^A-Za-z0-9._-]+`.
3. **Отказоустойчивость и повторные попытки (Retries)**:
   - При сетевых сбоях (HTTP 502/503/504) со стороны API Gateway клиент бэкенда выполняет до 3 попыток отправки с экспоненциальной задержкой (1s, 2s, 4s).

---

## 8. План внедрения в etranprocessing

1. **Этап 1: Добавление общего Email-клиента**
   - Разместить `ServerlessEmailClient` в `ProcessingBackend/backend/app/services/email_client.py` и `MenuBuilder/backend/app/services/email_client.py` (или в общем модуле утилит).
   - Вынести URL API Gateway в переменные окружения (`SERVERLESS_EMAIL_GATEWAY_URL`).
2. **Этап 2: Интеграция с биллингом и уведомлениями организаций**
   - Добавить фоновую отправку писем на `org.email` при смене статуса лицензий (`DUE_SOON`, `OVERDUE`, `DISABLED`) в `ProcessingBackend/backend/app/routers/licensebilling.py`.
   - Внедрить проверку флага `org.notify_by_email`.
3. **Этап 3: Подтверждение email в MenuBuilder**
   - Создать Alembic-миграцию для добавления `is_email_verified`, `email_verified_at` в `orgs` и создание таблицы `email_verifications`.
   - Добавить эндпоинты отправки и подтверждения токена в `MenuBuilder/backend/app/routers/admin_organizations.py`.
   - Добавить индикатор статуса (зеленый бэдж "Подтвержден" / кнопка "Подтвердить email") в интерфейс `MenuBuilder/frontend`.
4. **Этап 4: Автоматическая отправка отчетов**
   - Встроить фоновую отправку отчетов в роутеры `closeshift` и `inkass` в `ProcessingBackend/backend/app/routers/tech_gate.py`.
5. **Этап 5: Эксплуатационный мониторинг mTLS шлюза**
   - Проверить ротацию логов `/var/log/nginx/terem-email-mtls-access.log` на сервере `dev.leo4.ru`.
   - Настроить мониторинг кодов ответов Yandex API Gateway через `server-ops` MCP.
