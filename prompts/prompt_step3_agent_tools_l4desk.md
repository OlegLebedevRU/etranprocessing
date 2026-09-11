# PROMPT AGENT — `tools/l4desk`: Команда продления аренды `lease_renew`, reconcile-события и верификация recovery

Ты — Senior Windows / C Systems инженер. Работаешь автономно в репозитории `D:\repo\platerra\Public\etranprocessing`, каталог `tools/l4desk/` (разрешён явно).
Твоя цель — устранить преждевременную остановку FFmpeg локальным fail-closed watchdog'ом (добавить прием и обработку команды продления аренды `lease_renew`), оповещать сервер о принудительной остановке осиротевшего процесса при `reconcile`, и подготовить стенд к корректной проверке recovery.

Жёсткие ограничения:
- **Tools suite НЕ ПЕРЕПАКОВЫВАЕМ**: не запускать `pack_zip.cmd`, не трогать `tools/dist`, инсталлятор `l4install`.
- Работа только в `tools/l4desk/`.
- Сборка исключительно скриптом `tools/l4desk/build.cmd all`.
- Чистый Win32/C, без внешних DLL runtime, статическая линковка `/MT`, сборка под x86 и x64.

---

## 1. Контекст выявленных дефектов

### 1.1 Остановка FFmpeg через 20 секунд по `lease_expired`
- **Проблема:** Локальный fail-closed watchdog останавливает FFmpeg через 20 с после старта:
  ```text
  11:51:25 stream_start / FFmpeg started
  11:51:45 Local lease expired (... + 5s). Fail-closed stop.
  11:51:45 stream_event state=stopped, reason=lease_expired
  ```
- **Причина:** В `ctl_protocol.c:420-784` функция `ctl_handle_command` обрабатывает только:
  - `inventory_get`
  - `stream_start` (где выставляется начальный `expires_at_ms`)
  - `stream_stop`
  - `pointer_move` / `mouse_click` / `key_event`
  Команды продления аренды (`lease_renew`) в протоколе нет! При отсутствии входящих кликов мыши через MQTT таймер `g_sup.lease_expires_at_ms` не обновляется, и watchdog штатно глушит процесс.
- **Задача:** Добавить команду `lease_renew` в `ctl_handle_command`:
  - Проверять совпадение `lease_id`.
  - Вызывать `ffmpeg_supervisor_update_lease(lease_id, expires_at_ms)`.
  - Отправлять `ack` с обновленным сроком.

### 1.2 Оповещение сервера при `reconcile` (taskkill l4desk)
- **Проблема:** Когда `l4desk` падает или перезапускается (`taskkill l4desk.exe`), при повторном старте `ffmpeg_supervisor_reconcile()` находит старый процесс `ffmpeg.exe` и принудительно завершает его (`TerminateProcess`). Однако сервер `app1` не получает событие `stream_event`, и на UI статус висит «В эфире», хотя кадры больше не идут.
- **Задача:** В `ffmpeg_supervisor_reconcile()` при обнаружении и убийстве осиротевшего процесса отправлять событие:
  `notify_stream_event(stream_id, "stopped", "agent_restart_reconcile")`
  (или публиковать `stream_event` сразу после установления MQTT-соединения).

### 1.3 Методика проверки Recovery супервизора
- **Важное правило:** После `lease_expired` перезапускать FFmpeg **запрещено** политикой безопасности — это штатная остановка по безопасности.
- Автоматический перезапуск (recovery-loop супервизора с backoff до 5 попыток) срабатывает **только при неожиданном падении FFmpeg** (`unexpected_exit`), когда срок аренды еще активен.
- Тестирование recovery должно выполняться завершением **только процесса FFmpeg**:
  ```powershell
  Get-Process ffmpeg | Stop-Process -Force
  ```
  При этом в логе должна появиться цепочка:
  ```text
  FFmpeg process PID=... exited unexpectedly
  Published stream_event [restarting]
  Executing scheduled FFmpeg restart (attempt 1/5)...
  FFmpeg started successfully: PID=...
  Published stream_event [running] reason="recovered"
  ```

---

## 2. Задачи разработки

### 2.1 Поддержка `lease_renew` в `src/ctl_protocol.c`
1. В `ctl_handle_command(const char* payload, size_t payload_len, ...)` добавить ветку:
   ```c
   /* lease_renew / stream_renew */
   if (strcmp(cmd_type, "lease_renew") == 0 || strcmp(cmd_type, "stream_renew") == 0) {
       StreamStateInfo stream;
       ffmpeg_supervisor_get_info(&stream);

       if (strcmp(stream.state, "running") != 0 && strcmp(stream.state, "restarting") != 0) {
           int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                            "stream_not_running", "No active stream to renew", now_ms);
           if (len > 0) {
               *out_resp_len = (size_t)len;
               *p_should_publish = true;
               return true;
           }
           return false;
       }

       if (stream.lease_id[0] != '\0' && strcmp(stream.lease_id, lease_id) != 0) {
           int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                            "lease_mismatch", "Active stream lease does not match", now_ms);
           if (len > 0) {
               *out_resp_len = (size_t)len;
               *p_should_publish = true;
               return true;
           }
           return false;
       }

       if (expires_at_ms > 0) {
           ffmpeg_supervisor_update_lease(lease_id, (uint64_t)expires_at_ms);
           log_info("Lease renewed for stream %s: new expires_at_ms=%llu",
                    stream.stream_instance_id, (unsigned long long)expires_at_ms);
       }

       int len = ctl_build_ack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, now_ms);
       if (len > 0) {
           dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
           *out_resp_len = (size_t)len;
           *p_should_publish = true;
           return true;
       }
       return false;
   }
   ```

### 2.2 Оповещение о reconcile в `src/ffmpeg_supervisor.c`
В `ffmpeg_supervisor_reconcile()`:
Если найден и завершен осиротевший PID:
```c
if (actual_c == (uint64_t)creation_time) {
    log_warn("Confirmed orphaned process PID=%d creation_time matches. Terminating...", pid);
    TerminateProcess(hProc, 1);
    WaitForSingleObject(hProc, 3000);
    if (stream_id[0] != '\0') {
        notify_stream_event(stream_id, "stopped", "agent_restart_reconcile");
    }
}
```

---

## 3. Сборка и верификация

1. Сборка бинарников:
   ```cmd
   cd D:\repo\platerra\Public\etranprocessing\tools\l4desk
   build.cmd all
   ```
2. Проверка артефактов:
   - `bin\x86\l4desk.exe`
   - `bin\x64\l4desk.exe`
   - `bin\l4desk.exe`
3. Обновление `CHANGELOG.md` и `README.md`.
