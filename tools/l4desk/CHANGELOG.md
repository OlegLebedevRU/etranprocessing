# CHANGELOG: l4desk

## [1.0.0] - 2026-09-09

### Added
- Первоначальный релиз терминального агента удалённого ввода `l4desk.exe`.
- Zero-dependency архитектура на C / Win32 API со статической линковкой `/MT`.
- Встроенный клиент MQTT 3.1.1 поверх WinSock2 для подключения к localhost Mosquitto (`client_id = svc_desk`).
- LWT и периодический статус Presence в `dev/<SN>/ctl` (`retain=1, qos=1`).
- Подписка на команды управления `srv/<SN>/ctl` (QoS 1).
- Обработка `pointer_move` (best-effort без ACK, нормализованные координаты `0..65535`).
- Обработка `mouse_click` с подтверждением ACK / NACK (QoS 1, `retain=0`).
- Инъекция ввода через `SendInput` с флагами `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK`.
- Определение состояния интерактивного экрана через `OpenInputDesktop` (детекция блокировки экрана / UAC).
- Bounded LRU-кеш дедупликации (256 записей) по `command_id`.
- Обнаружение серийного номера (SN) терминала через WinHTTP `GET http://127.0.0.1:18443/_leo4/sn`.
- Поддержка запуска в пользовательской сессии через `l4superv` (`--run`) и отладочного режима (`--console`).
- Совместимость с Windows 7 SP1+ (x86) и Windows 10/11 (x64).
