# PROMPT AGENT — шаг 5 / задание 3: `tools/l4superv` — такт ожидания активации (Active Wait), `pending_pin.json`, `installed_version`, форс-такт (Этап 1, **без** Update Agent)

Ты Senior Windows/C Systems инженер: C/Win32, службы SCM, MSVC `/MT`, x86 + x64. Разрешенный проект — `tools/l4superv` (`src/orchestrator.c`, `state_mgr.c`, `supervisor_main.c`, `config.c`, `proxy_client.c`, `res/`, `README.md`, `CHANGELOG.md`, тесты). **Запрещено** менять `installer_main.c`/`zip_extractor.c`/`service_mgr.c` (их переиспользует задание 2), `tools/l4pin` (задание 1 — только подключить готовый `cert_discovery.c/.h`), `l4desk`, `leo4proxy`, backend/frontend.

Прочитай [общий overview](prompt_step5_stacks_overview.md): разделы 2.1 (факты об `orchestrator.c`), 4.1 (Cert Discovery), 4.4 (контракт `pending_pin.json`, форс-такт, `state.json`), 5 (стенд), 6 (владелец). План: `docs/term_tool-zero-touch-installer-and-remote-runtime-plan.md`, п. 3.2 S9/S10, Пакет 3 п. 4–6. Пакет 6 (Update Agent, `dev/{SN}/tools`, манифесты) — **вне скоупа**, любые его элементы в этом задании отклоняются.

## 1. Факты

- `orchestrator.c`: такт по `watchdog_interval_sec` (10 с на стенде). Состояние определяется по `_leo4/info` `leo4proxy` (`proxy_client.c`): нет сертификата → `mosquitto_conf_generate_standby`, `state->status = "standby"`; появился → active-конфиг с мостом, запуск `l4desk` в интерактивной сессии; при смене `hw_fingerprint` и `auto_reset_on_clone` — удаление сертификатов и standby (эту политику **не менять** в Этапе 1, только логировать результат Cert Discovery рядом).
- `state.json` (`state_mgr.c`): `status, sn, thumbprint, not_after, hw_fingerprint, installer_base_path, services{…}, last_check, updated_at`. Поля `installed_version`, `installer_summary_path` отсутствуют.
- Служба не обрабатывает пользовательские `SERVICE_CONTROL` коды (проверить `supervisor_main.c` — `HandlerEx` и принимаемые controls).
- Нет никакого механизма отложенного PIN; `l4pin.exe` супервизор не вызывает.
- Стенд: `L4Superv` `Running`, `status: active`, сертификат валиден. Сценарий standby на стенде воспроизводится **только** при согласии владельца (см. 6 overview) — по умолчанию проверять на ВМ или через тестовый двойник `_leo4/info`.

## 2. Задачи

1. **Cert Discovery в такте:** подключить `../l4pin/src/cert_discovery.c` (или скопировать как есть с пометкой источника) и при каждом такте в standby логировать `cert_state`; при `CERT_VALID`, но `_leo4/info.certificate_found == false` дольше 2 тактов — записать предупреждение `proxy_cert_mismatch` (leo4proxy не видит сертификат: другой store/ACL ключа) — только лог, без действий.
2. **Такт ожидания активации (S10):** в standby сократить интервал опроса `_leo4/info` до 5 с (конфиг `standby_poll_sec`, по умолчанию 5; в active — прежние `watchdog_interval_sec`). Переход standby → active должен завершаться (active `mosquitto.conf`, мост, запуск `l4desk`) за ≤ 15 с после появления сертификата — измерить и зафиксировать.
3. **`pending_pin.json` (4.4 overview):** каждые 30 с в standby: если файл есть — проверить `schema`, `expires_at` (истек → перезаписать нулями, удалить, лог `pending_pin_expired`), расшифровать `pin_dpapi` (`CryptUnprotectData`, machine scope; ошибка → удалить файл, лог), проверить доступность CA (тот же URL-finder, что в `l4pin`: `_leo4/info.listeners.http_local` либо `https://iot-processing.ru`; достаточно TCP/TLS connect с таймаутом 5 с), запустить `%base%\l4pin\l4pin.exe <PIN>` с таймаутом 60 с, stdout → лог с маскировкой `pin=`. Успех (код `0` и `cert_discover` → VALID) → удалить файл, форс-такт; `25`-подобный отказ CA (неверный PIN) → удалить файл, лог `pending_pin_rejected`; сетевая ошибка → оставить до следующего такта. PIN в памяти — `SecureZeroMemory` сразу после `CreateProcessW`. Командную строку с PIN не логировать.
4. **Форс-такт:** принять `SERVICE_CONTROL 128` в `HandlerEx` (`SERVICE_ACCEPT_*` не требуется для user-defined; проверить), установить событие, по которому оркестратор немедленно выполняет такт. Также поддержать CLI `l4superv.exe --tick` (посылает control 128 через `ControlService`) для инженера и тестов.
5. **`state.json`:** добавить `installed_version` (строка SemVer, источник — `VERSIONINFO` самого `l4superv.exe` при отсутствии значения от инсталлятора; если инсталлятор записал — не затирать), `installer_summary_path`, `last_cert_state`. Сохранять при перезаписи все неизвестные ключи (проверить, как `state_mgr.c` сериализует — если он пишет фиксированный набор полей, расширить его, а не терять чужие).
6. **Диагностика:** строки лога `[WAIT] standby: cert_state=absent pending_pin=no ca=unknown next=5s`, `[WAIT] pending_pin found, expires_at=…, ca=reachable → l4pin`, `[WAIT] activation completed in N ms`. Ротация лога не менять.
7. `README.md`, `CHANGELOG.md`, `l4superv.json` пример (`standby_poll_sec`, `pending_pin_check_sec`). Поднять версию в `res/`.

## 3. Проверки

- `cmd /c build.cmd all` из `tools/l4superv` — x86/x64 без предупреждений; существующие тесты (если есть runner) проходят.
- Unit/компонентные тесты: разбор/валидация `pending_pin.json` (валидный, истекший, битый base64, чужой DPAPI-scope), машина состояний такта (cert_state × pending_pin × ca_reachable → действие), корректная перезапись `state.json` с сохранением неизвестных ключей, маскировка PIN.
- Компонентный тест с двойником `leo4proxy`: локальный HTTP-стаб `_leo4/info` на другом порту (конфигурируемый `proxy_info_url` — если его нет в `config.c`, добавить) с переключением `certificate_found: false → true`; измерить время до `active` (≤ 15 с) и до запуска `l4desk` (в тесте — заглушка `l4desk.exe`, печатающая сессию).
- Стенд (эта машина), **безопасные** проверки после подтверждения владельца, что службу можно перезапустить: установить собранный `l4superv.exe` поверх (бэкап предыдущего в `C:\l4tools\rollback\`), `Restart-Service L4Superv` → `state.json` получил `installed_version`, `last_cert_state: valid`, статус остался `active`, thumbprint не изменился, `l4desk.exe` перезапущен в интерактивной сессии; `l4superv.exe --tick` → в логе немедленный такт. `pending_pin.json` на стенде **не создавать** с реальным PIN; допустимо создать файл с заведомо неверным PIN (`000000`) **только** если владелец подтвердит, что лишний отклоненный запрос к CA допустим — иначе пропустить.
- Проверить, что `auto_reset_on_clone` не сработал ложно (hw_fingerprint стенда не изменился после установки).

## 4. Результат

Верни: diff по `orchestrator.c`/`state_mgr.c`/`supervisor_main.c`, таблицу машины состояний такта, замер standby→active из компонентного теста, `state.json` стенда «до/после» (без секретов), хэши `bin\x86\l4superv.exe`, `bin\x64\l4superv.exe`, список непроверенного на реальном стенде (ожидаемо: S10 с реальным PIN). Расхождения с 4.4 overview — только через правку overview.
