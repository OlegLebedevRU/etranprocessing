# API Reference: Device Connection, Collision Status & Audit Log

## 1. Обзор структуры `connection`

Объект `connection` в ответах API (`GET /api/v1/devices/`) содержит актуальный материализованный снимок транспортного и сервисного состояния связи терминала с платформой.

### Базовые поля объекта `connection`

| Поле | Тип | Описание |
|---|---|---|
| `device_id` | `int` | Идентификатор устройства в системе |
| `client_id` | `str` | Серийный номер (`SN`) устройства / Common Name сертификата |
| `connected_at` | `datetime (ISO 8601)` | Время открытия физического TCP/TLS сокета на брокере |
| `checked_at` | `datetime (ISO 8601)` | Время последнего обновления или сверки статуса в БД |
| `last_checked_result` | `bool` | Физический статус открытого сокета (`true` — подключен, `false` — отключен) |
| `app_connect` | `bool \| null` | Статус сервиса приложения по LWT-каналу `dev/<SN>/app` (`null` — LWT не поддерживается) |
| `svc_connect` | `bool \| null` | Статус системного демона по LWT-каналу `dev/<SN>/svc` |
| `is_app_available` | `bool \| null` | Вычисляемая готовность приложения: `(last_checked_result and app_connect and not is_blocked)` |
| `is_svc_available` | `bool \| null` | Вычисляемая готовность сервиса: `(last_checked_result and svc_connect and not is_blocked)` |
| `details` | `object \| null` | Детализированная телеметрия сокета из брокера (`peer_host`, `peer_port`, `ssl_cipher`, `peer_cert_validity`) |

---

## 2. Поля безопасности и фиксации нарушений

При выявлении аномалий безопасности объект `connection` обогащается следующими полями:

| Поле | Тип | Описание |
|---|---|---|
| `is_blocked` | `bool` | Флаг блокировки устройства (`true` — доступ заблокирован из-за коллизии) |
| `violation_type` | `str \| null` | Тип нарушения: `DEVICE_CLONE` (клон накопителя) или `SN_COLLISION` (дубликат сертификата) |
| `violation_details` | `object \| null` | Метаданные инцидента: список конфликтующих IP-адресов, даты сертификатов, время фиксации |
| `recent_audit_events` | `list[DeviceAuditEvent] \| null` | Топ-5 последних событий жизненного цикла (присутствует **только** при запросе конкретного `device_id`) |

---

## 3. Примеры ответов API

### Пример 1: Выдача в общем списке `GET /api/v1/devices/` (с зафиксированным клоном)

В общем списке возвращается компактная материализация без раздувания объема трафика:

```json
[
  {
    "id": 147,
    "device_id": 6209,
    "sn": "a4b0006209c67756d020626",
    "connection": {
      "device_id": 6209,
      "client_id": "a4b0006209c67756d020626",
      "connected_at": "2026-08-30T08:33:07.448000Z",
      "checked_at": "2026-08-30T08:33:07.470000Z",
      "last_checked_result": false,
      "app_connect": null,
      "svc_connect": null,
      "is_app_available": null,
      "is_svc_available": null,
      "is_blocked": true,
      "violation_type": "DEVICE_CLONE",
      "violation_details": {
        "detected_at": "2026-08-30T08:33:07.470000Z",
        "violation_type": "DEVICE_CLONE",
        "conflicting_hosts": ["83.237.254.238", "91.242.213.17"],
        "cert_validities": ["2026-08-07T06:42:22Z - 2027-08-07T06:42:22Z"],
        "flapping_count": 5,
        "host_switches": 4,
        "reason": "Rapid IP hopping across distinct hosts with identical credentials"
      },
      "recent_audit_events": null
    },
    "device_gauges": [],
    "device_tags": []
  }
]
```

### Пример 2: Детальный запрос терминала `GET /api/v1/devices/?device_id=6209`

При фильтре по конкретному терминалу возвращается расширенная история (топ-5 событий аудита):

```json
[
  {
    "id": 147,
    "device_id": 6209,
    "sn": "a4b0006209c67756d020626",
    "connection": {
      "device_id": 6209,
      "client_id": "a4b0006209c67756d020626",
      "last_checked_result": false,
      "is_blocked": true,
      "violation_type": "DEVICE_CLONE",
      "violation_details": {
        "detected_at": "2026-08-30T08:33:07.470000Z",
        "conflicting_hosts": ["83.237.254.238", "91.242.213.17"],
        "reason": "Rapid IP hopping across distinct hosts with identical credentials"
      },
      "recent_audit_events": [
        {
          "id": 12,
          "device_id": 6209,
          "org_id": 339,
          "event_type": "DEVICE_CLONE",
          "actor": "system/detector",
          "details": {
            "conflicting_hosts": ["83.237.254.238", "91.242.213.17"],
            "flapping_count": 5
          },
          "created_at": "2026-08-30T08:33:07.470000Z"
        },
        {
          "id": 3,
          "device_id": 6209,
          "org_id": 339,
          "event_type": "PROVISIONED",
          "actor": "api/provisioning",
          "details": {
            "sn": "a4b0006209c67756d020626",
            "name": "Terminal 6209"
          },
          "created_at": "2026-08-07T06:42:22.000000Z"
        }
      ]
    }
  }
]
```

---

## 4. Действия потребителя при получении статуса `is_blocked = true`

1. Проверить значение `violation_type`:
   - `DEVICE_CLONE`: обнаружены клоны жесткого диска. Требуется физически отключить дублирующие терминалы и выпустить новые уникальные сертификаты.
   - `SN_COLLISION`: обнаружен старый терминал со старым сертификатом. Требуется отключить старый терминал.
2. Провести перепровиженинг устройства через `POST /api/v1/internal/provisioning/terminals`. Это автоматически снимет блокировку и сбросит флаги нарушений.
