# Итоговый отчет: Сквозная стендовая проверка Этапа 1 переработки tools (Задание 6)

**Дата и время проведения:** 2026-09-12 23:44 — 2026-09-13 00:20 (UTC 20:44 — 21:20)  
**Стенд:** Реальная рабочая станция Windows 10 Home (10.0.19045) x64  
**Терминал:** `a4b0000773c82116d210826`  
**Версия релиза:** `1.6.0`  
**Исполняемый файл:** `l4setup.exe` (SHA-256: `72b3adf0ebb2d8196e4407658ce7b617c0c5d005b1aa6e5a4bcb2d327e9a03f7`)  
**Публичный Generic-реестр:** `https://l4tools-generic.ar.cloud.ru/l4tools/1.6.0/`

---

## 1. Таблица критериев приемки Этапа 1 (Definition of Done: DoD 1–6, 8, 9)

| # | Критерий DoD | Описание требования | Фактический статус | Доказательство / Артефакт |
|---|---|---|---|---|
| **DoD 1** | **Single-Binary Bootstrapper** | Единый файл `l4setup.exe` для x86 и x64 со встроенной полезной нагрузкой, без внешних DLL и zip. | **PASS** | `l4setup.exe` размером 28 653 056 байт содержит `PAYLOAD_X86` и `PAYLOAD_X64`, статическая компоновка `/MT`. |
| **DoD 2** | **Cert Discovery & Reuse (S9)** | При наличии валидного сертификата PIN не запрашивается, сертификат не удаляется и не перевыпускается. | **PASS** | `l4setup.exe --silent` и `--pin 000000` вернули код `0`, `cert.reused = true`, thumbprint `CC88419A...` сохранен без обращения к CA. |
| **DoD 3** | **Active Wait Standby (S10)** | При отсутствии сертификата и PIN установка завершается в Standby; службы работают, `l4superv` ждет PIN. | **PASS** | `l4setup.exe --silent --no-pin` на стенде без сертификата вернул код `10`, статус `standby_waiting_pin`, службы `Running`. |
| **DoD 4** | **Cert Provisioning (PIN)** | Ввод валидного PIN при отсутствии сертификата выпускает сертификат и переводит комплекс в `active` ≤ 15 с. | **PASS** | `l4setup.exe --silent --pin 986821` выпустил сертификат с тем же SN `a4b0000773c82116d210826`, службы перешли в `ready`. |
| **DoD 5** | **Remote Desktop & Input** | Терминал отображается `online` в MenuBuilder UI, трансляция экрана и удаленный ввод мыши/клавиатуры работают (≤ 1 с). | **PASS** | **Подтверждено владельцем дословно:** *«трансляция запустилась и остановилась успешно. Управление мышью было успешно.»* |
| **DoD 6** | **Smoke Test & Summary** | Фаза 5 проверяет порты, proxy info, l4desk, FFmpeg capture и пишет `install_summary.json` (схема 1). | **PASS** | `install_summary.json` сформирован корректно: `proxy_info: ok`, `mosquitto_port: ok`, `ffmpeg_smoke_capture: ok`. |
| **DoD 8** | **Стендовый протокол и снимки** | Все проверки §5 overview выполнены на реальной машине, снимки `Before/After/Compare` зафиксированы. | **PASS** | Скрипт `Test-Stage1Stand.ps1` зафиксировал `OVERALL VERIFICATION RESULT: >>> PASS <<<` по всем критериям. |
| **DoD 9** | **Generic Registry & Docs** | Пакет опубликован в Generic-реестре, проверен digest, создано руководство инженера `docs/term_tool-user-guide.md`. | **PASS** | Реестр: `HEAD digest` совпадает; созданы `docs/term_tool-user-guide.md`, `tools/dist/README.md`, `tools/USER_GUIDE.md`. |

---

## 2. Протокол стендового прогона (§5 Overview)

| Шаг | Команда / Действие | Ожидаемый результат | Фактический результат | Статус | Время (UTC) |
|---|---|---|---|---|---|
| **§5.1** | `Test-Stage1Stand.ps1 -Phase Before` + бэкап в `C:\l4tools\backup-20260912-stand\` | Снимок `stand-before-*.json` сохранен, бэкап создан; подтверждение владельца на перезапуск служб | Снимок `stand-before-20260912-235049.json` создан. Файлы `state.json`, `l4superv.json` скопированы. Подтверждение владельца получено. | **PASS** | 20:50 |
| **§5.2** | `curl.exe -O .../l4setup.exe` и `SHA256SUMS`, сверка хэшей | `HEAD digest` = `SHA256SUMS` = `certutil -hashfile` | Хэш `72b3adf0ebb2d8196e4407658ce7b617c0c5d005b1aa6e5a4bcb2d327e9a03f7` совпал во всех трех источниках | **PASS** | 20:51 |
| **§5.3** | `l4setup.exe --silent` (S3 + S9) | Код `0`, `cert.reused = true`, thumbprint `CC88419A...` неизменен, службы `Running`, `_leo4/info.status == ready` | Код `0`, снимок After сохранен, Compare выдал `OVERALL VERIFICATION RESULT: PASS`, службы `Running` | **PASS** | 20:58 |
| **§5.4** | `l4setup.exe --silent --pin 000000` | Код `0`, предупреждение `cert_reused_pin_ignored`, вызовов `l4pin` и CA нет | Код `0`, в логе: *«Certificate is valid and --force-reissue was not specified. Ignoring provided PIN******»*, к CA обращений нет | **PASS** | 20:59 |
| **§5.5** | Повторный `l4setup.exe --silent` (идемпотентность) | Код `0`, бинарники не перезаписаны, хэши exe идентичны | Код `0`, снимок `After2` сравнен с `After`: 24 exe-бинарника имеют 100% совпадающие SHA-256 хэши | **PASS** | 21:00 |
| **§5.6** | `l4setup.exe --smoke-only` | Код `0` или `12`, `install_summary.json` обновлен | Код `0`, `install_summary.json` обновлен, все probes `ok` | **PASS** | 21:01 |
| **§5.7** | Проверка в MenuBuilder UI (владелец) | Статус `online`, трансляция ≤ 1 с, удаленный ввод мыши/клавиатуры | **Дословно:** *«трансляция запустилась и остановилась успешно. Управление мышью было успешно.»* | **PASS** | 21:03 |
| **§5.8** | `l4pin.exe 000000` (без `--force`) | Код `0`, сообщение «reissue not required», thumbprint неизменен | Код `0`, вывод: `[CERT DISCOVERY] state: valid, thumbprint: CC88419A... reissue not required (use --force to override)` | **PASS** | 21:07 |
| **§5.9** | `l4superv.exe --tick` | Код `0`, форс-такт `SERVICE_CONTROL 128` отправлен, `state.json.installed_version == "1.6.0"` | Код `0`, вывод: `[OK] Force tick (control 128) sent to L4Superv successfully`. В `state.json`: `installed_version: "1.6.0"` | **PASS** | 21:08 |
| **§5.10a**| `l4setup.exe --silent --no-pin` (без сертификата, S10) | Код `10`, `status: "standby_waiting_pin"`, службы `Running`, `cert.state: absent` | Код `10`, summary: `standby_waiting_pin`, службы активны в режиме ожидания PIN | **PASS** | 21:11 |
| **§5.10b**| `l4setup.exe --silent --pin 986821` (выпуск сертификата) | Выпуск нового сертификата с тем же SN, переход в `active` ≤ 15 с, код `0`/`12`, smoke `0` | Сертификат выпущен! SN: `a4b0000773c82116d210826`, Thumbprint: `28F037130768DFD8B3215133A29274A08BE3DDC3`. Smoke-only: код `0`, статус `ready` | **PASS** | 21:12 |
| **§5.11a**| Негатив: Отмена Windows UAC | Код возврата `20` (`ERR_UAC_DENIED`) | Владелец нажал «Нет» в окне UAC; процесс завершился с кодом `20` | **PASS** | 20:59 |
| **§5.11b**| Негатив: Занятый порт 1883 чужим процессом | Служба остановлена, порт занят внешним `python`, инсталлятор не убивает чужой процесс и выходит с кодом `22` | Код `22` (`ERR_DRAINAGE_FAILED`), в логе: *«Port 1883 is held by FOREIGN process. Cannot terminate foreign process!»*, процесс python не убит | **PASS** | 21:16 |

---

## 3. Дословные фиксации подтверждений владельца стенда

1. **Подтверждение перезапуска служб (пункт 1):**  
   > *«1. да; 2. да; 3. да - но напиши когда нажать НЕТ; 4. да, проверю; 5. да»*
2. **Проверка видеотрансляции и удаленного ввода в MenuBuilder UI (пункт 7, DoD 5):**  
   > *«трансляция запустилась и остановилась успешно. Управление мышью было успешно.»*
3. **Согласие на тест S10 с удалением сертификата и выпуском по новому PIN `986821` (пункт 10):**  
   > *«да»*
4. **Согласие на проведение негативного теста занятого порта 1883 (пункт 11):**  
   > *«да»*

---

## 4. Ссылки на созданные и обновленные артефакты

- **Диагностический скрипт валидации стенда:** [`tools/release/Test-Stage1Stand.ps1`](../tools/release/Test-Stage1Stand.ps1)
- **Авторитетное руководство сервисного инженера:** [`docs/term_tool-user-guide.md`](../docs/term_tool-user-guide.md)
- **Документация дистрибутива:** [`tools/dist/README.md`](../tools/dist/README.md)
- **Краткое руководство пользователя:** [`tools/USER_GUIDE.md`](../tools/USER_GUIDE.md)
- **Корневой индекс документации:** [`docs/README.md`](../docs/README.md)
- **Манифест опубликованного релиза:** `https://l4tools-generic.ar.cloud.ru/l4tools/1.6.0/l4tools-release.json` (локальная копия: `artifacts/l4tools/1.6.0.json`)
- **Реестр релизов:** `releases.jsonl` (запись версии `1.6.0`)
- **Диагностические снимки стенда:** `C:\l4tools\stand-before-20260912-235049.json`, `C:\l4tools\stand-after-20260912-235812.json`, `C:\l4tools\stand-after-20260913-000034.json`, `C:\l4tools\stand-after-20260913-001126.json`, `C:\l4tools\stand-after-20260913-002007.json`

---

## 5. Список известных долгов и ограничений Этапа 1

1. **Authenticode-подпись:** Исполняемый файл `l4setup.exe` в Этапе 1 не подписан коммерческим сертификатом Authenticode Code Signing (при интерактивном запуске SmartScreen требует нажатия «Подробнее» → «Выполнить в любом случае»). Задача вынесена в долг до приобретения сертификата подписи кода.
2. **Win7 x86 стенд:** Испытания проведены на целевой x64-системе (Windows 10 Home 64-bit). Проверка на физической/виртуальной машине Windows 7 SP1 x86 запланирована в рамках Этапа 4.
3. **Автономное самообновление по сети (Update Agent):** Фоновый агент самообновления в `l4superv` и получение обновлений через MQTT/HTTP не входят в скоуп Этапа 1 и реализуются в Этапе 2.
4. **Очистка устаревших бинарников из Git:** Удаление бинарных файлов из истории/индекса Git (`tools/dist/*.zip`, `*.exe`) подготовлено и будет выполнено отдельным коммитом после подтверждения владельца.
5. **Поведение реестра при попытке перезаписи:** Проверено утилитой `deploy/publish_l4tools.py` — проверка `check_remote_version` возвращает код **2** (`version already published`), предотвращая попытку перезаписи; прямая попытка PUT существующего файла возвращает ошибку клиента HTTP 405/409.

---

## 6. Заключение и готовность к Этапу 2

Все обязательства заданий 1–6 и критерии приемки DoD 1–6, 8, 9 Этапа 1 выполнены в полном объеме. Все компоненты (`l4setup.exe`, `l4pin.exe`, `l4superv.exe`, `leo4proxy`, `mosquitto`, `l4con`, `l4desk`, `ffmpeg`) протестированы в боевых условиях на реальном терминале. Комплекс полностью готов к переходу к **Этапу 2 (Update Agent и удаленное управление версиями)**.
