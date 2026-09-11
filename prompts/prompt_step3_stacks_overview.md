# Обзор и координация промптов шага 3: Watchdog, Lease Keepalive, Reconcile & UX Remote Control

Данный документ координирует устранение проблем, выявленных в ходе тестирования видеонаблюдения и удаленного управления терминалами (E2E Video & Remote Input).

---

## 1. Состав стеков и изолированные промпты

| Стек / Репозиторий | Роль и технология | Документ промпта | Ключевая ответственность |
|---|---|---|---|
| **`MenuBuilder` (BFF + UI)**<br>`D:\repo\platerra\Public\etranprocessing` | Senior Full-Stack<br>(Python 3.14 FastAPI + React 19 / TS) | [`prompt_step3_agent_menubuilder.md`](prompt_step3_agent_menubuilder.md) | **Деплой бэкенда и устранение 404/409:** актуализация `menubuilder-backend` на проде (исправление 404 на `/control/keepalive`), идемпотентность `stream/stop` (устранение 409 Conflict), исправление `onWheel` passive listener в UI, блокировка `sendMove` при неактивном desktop. |
| **`iot-rpc-rest-app` (`app1`)**<br>`D:\work\iot.leo4.ru\iot-rpc-rest-app` | Senior Python Backend<br>(Python 3.14, FastAPI, RabbitMQ, MQTT) | [`prompt_step3_agent_iot_rpc_rest_app.md`](prompt_step3_agent_iot_rpc_rest_app.md) | **Трансляция продления аренды в MQTT:** при вызове keepalive публикация команды `lease_renew` в `srv/{SN}/ctl`, сохранение `stream_mode="desktop"` при scope upgrade, сброс состояния стрима по терминальному `stream_event`. |
| **`tools/l4desk`**<br>`D:\repo\platerra\Public\etranprocessing` | Senior Windows / C Systems<br>(C / Win32, MSVC `/MT`, x86 + x64) | [`prompt_step3_agent_tools_l4desk.md`](prompt_step3_agent_tools_l4desk.md) | **Обработка `lease_renew` и Reconcile:** прием команды продления аренды в `ctl_protocol.c` для предотвращения остановки FFmpeg локальным fail-closed watchdog'ом через 20 с, отправка `stream_event(stopped, agent_restart_reconcile)` при убийстве осиротевшего процесса, чистая сборка. |

---

## 2. Сквозная логика продления аренды (Keepalive Chain)

```
[ Браузер: MenuBuilder UI ]
        │  Периодический keepalive (раз в 5 с)
        ▼
[ MenuBuilder Backend: /devices/{id}/control/keepalive ]
        │  REST keepalive
        ▼
[ iot-rpc-rest-app (app1): /lease/{id}/keepalive ]
        │  1. Продление lease.expires_at в памяти
        │  2. Публикация команды lease_renew (QoS 1)
        ▼
[ RabbitMQ MQTT / Mosquitto: топик srv/{SN}/ctl ]
        │
        ▼
[ Terminal Agent: tools/l4desk ]
        │  1. Прием команды lease_renew
        │  2. ffmpeg_supervisor_update_lease(lease_id, expires_at_ms)
        │  3. Watchdog таймер сдвинут вперед (+5с grace)
        ▼
[ FFmpeg ] продолжает стабильное кодирование H.264 без остановки!
```

---

## 3. Методика проверки Recovery (самовосстановления)

- **Внимание:** Остановка по `lease_expired` является штатной безопасной остановкой — супервизор **не должен** перезапускать FFmpeg при истечении аренды.
- Для проверки recovery стрим должен стабильно удерживаться дольше 1 минуты благодаря продлению аренды.
- Проверка автовосстановления выполняется принудительным завершением **только процесса FFmpeg**:
  ```powershell
  Get-Process ffmpeg | Stop-Process -Force
  ```
  Ожидается автоматический перезапуск процесса агентом `l4desk` и продолжение WebRTC-вещания.
