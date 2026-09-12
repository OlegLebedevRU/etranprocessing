# PROMPT AGENT — шаг 5 / задание 1: `tools/l4pin`, модуль Cert Discovery и guard «не трогать валидный сертификат»

Ты Senior Windows/C Systems инженер: C/Win32, CryptoAPI/CNG, MSVC `/MT`, x86 + x64. Разрешенный проект — `tools/l4pin` (его `src/`, `build.cmd`, `CMakeLists.txt`, `README.md`, `USER_GUIDE.md`, `CHANGELOG.md`). `tools/l4superv` и `tools/leo4proxy` — только read-only чтение для сверки форматов. Не трогать backend/frontend/другие утилиты, не создавать `tools/l4setup` (это задание 2).

Прочитай [общий overview](prompt_step5_stacks_overview.md): раздел 2.1 (факты о `cert_store.c`), раздел 4.1 (контракт Cert Discovery — он обязателен и не меняется без правки overview), раздел 5 (стенд), раздел 6 (владелец).

## 1. Факты

- `tools/l4pin/src/cert_store.c` (около строк 260–300): перед установкой нового сертификата перебирает `LocalMachine\MY` и удаляет **все** сертификаты с тем же email (`cert_get_email`), затем `CertAddCertificateContextToStore(..., CERT_STORE_ADD_REPLACE_EXISTING)` и привязка `CERT_KEY_PROV_INFO_PROP_ID` к CNG-контейнеру (`MS_KEY_STORAGE_PROVIDER`, `CRYPT_MACHINE_KEYSET`, `dwKeySpec = 0`).
- Нигде в `l4pin` нет проверки «сертификат уже есть и годен» до похода в CA. Любой запуск `l4pin.exe <PIN>` расходует PIN и меняет thumbprint/serial.
- На стенде (эта машина) в `LocalMachine\MY` ровно один целевой сертификат: `CN=a4b0000773c82116d210826`, issuer `CN=iot.leo4.ru, O=Leo4`, `NotAfter 2027-08-29 17:09:40 UTC`, thumbprint `CC88419A4C3763150A4C0905261EC073C58CFC09`, приватный ключ CNG есть, **неэкспортируемый**. Его нельзя удалить/перевыпустить без нового PIN от владельца.
- `leo4proxy` `_leo4/info` уже отдает `certificate_found`, `thumbprint`, `serial`, `not_after`, `has_private_key`, `sn` — формат дат `YYYY-MM-DD HH:MM:SS UTC`. Держать совместимость строк дат там, где они сравниваются.
- `state.json` (`C:\l4tools\state.json`) содержит `sn` и `thumbprint`; `expected_sn` для Cert Discovery берется оттуда вызывающей стороной, `l4pin` сам `state.json` не читает (кроме режима `--check --sn <SN>` из CLI).

## 2. Задачи

1. **Новый модуль `tools/l4pin/src/cert_discovery.c` + `cert_discovery.h`** строго по контракту 4.1 overview: `cert_discover(expected_sn, &info)` → `CERT_VALID | CERT_EXPIRING | CERT_BROKEN | CERT_ABSENT`. Критерии кандидата: issuer CN содержит `iot.leo4.ru`; `CryptAcquireCertificatePrivateKey(..., CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_CACHE_FLAG, ...)` успешен; `NotAfter > now`. Несколько кандидатов — выбрать с максимальным `NotAfter`, посчитать `cert_duplicates`. Никаких вызовов удаления в этом модуле. Модуль должен собираться и как часть `l4pin`, и как самостоятельные `.c/.h` для включения в `l4setup`/`l4superv` (без зависимостей на остальной код `l4pin`, только `crypt32`, `ncrypt`, `advapi32`).
2. **CLI `l4pin.exe --check [--sn <SN>] [--json]`**: печатает состояние человеку или JSON по 4.1; коды возврата `0/1/2/3`, `>= 20` — ошибка доступа к хранилищу (например, без прав администратора — сообщить внятно). Без сетевых обращений.
3. **Guard в основном сценарии `l4pin.exe <PIN>`**: перед шагом `function=check` вызвать `cert_discover(NULL или --sn)`; при `CERT_VALID` и отсутствии `--force` — напечатать «Certificate <thumbprint> for <SN> is valid until <NotAfter>; reissue not required (use --force to override)», код `0`, **ни одного** сетевого запроса и ни одного изменения хранилища. При `CERT_EXPIRING` без `--force` — предупредить и продолжить выпуск (истекает); при `CERT_BROKEN`/`CERT_ABSENT` — прежний путь. `--force <PIN>` — прежнее поведение полностью.
4. **Удаление по email** оставить только в ветке реального выпуска (после успешного ответа CA), как сейчас; не расширять критерий удаления. Логировать, какие сертификаты удалены (thumbprint), без PIN и без тела ответов CA.
5. **Диагностика**: в лог `l4pin` добавить строку результата Cert Discovery (state, thumbprint, days_left, duplicates). Не логировать PIN ни в каком виде (проверить существующие `printf` с URL — `pin=` должен маскироваться `pin=***`).
6. Обновить `README.md`, `USER_GUIDE.md`, `CHANGELOG.md` `l4pin` (новые флаги, коды возврата, семантика «не трогать валидный»). Поднять версию в `res/` (`VERSIONINFO`), если она там есть.

## 3. Проверки

- Сборка `cmd /c build.cmd all` из `tools/l4pin` без предупреждений на x86 и x64 (`/W4`, как принято в проекте — сверить с текущими флагами).
- Тесты модуля: добавить `tests/test_cert_discovery.c` (или расширить существующий runner, если есть) с фикстурами на **временном** хранилище: создать самоподписанный сертификат с issuer `iot.leo4.ru` через `CertCreateSelfSignCertificate` в `CERT_STORE_PROV_MEMORY` и проверить ветки VALID / EXPIRING (NotAfter через 10 дней) / BROKEN (без ключа; чужой CN) / ABSENT / дубликаты. Реальное `LocalMachine\MY` в тестах не изменять.
- На стенде (эта машина, от администратора):
  - `l4pin.exe --check --json` → `state: valid`, thumbprint `CC88419A…`, `days_left ≈ 350`, код `0`.
  - `l4pin.exe --check --sn WRONGSN` → `broken`, код `2`.
  - `l4pin.exe 000000` (заведомо неверный PIN, **без** `--force`) → код `0`, сообщение о валидном сертификате, в логе нет `function=check`, thumbprint в `Cert:\LocalMachine\My` не изменился, `_leo4/info` не изменился.
  - **Не выполнять** `l4pin.exe --force <PIN>` на стенде без явного «да» владельца и нового PIN в чате.
- Убедиться, что `l4superv` (`orchestrator.c`) и текущий `l4install` не зависят от прежнего кода `0`/`1` `l4pin` иначе, чем задокументировано; если зависят — описать в отчете, не менять `l4superv`.

## 4. Результат

Верни: список файлов, публичный API `cert_discovery.h` (буквально), вывод `--check --json` со стенда, доказательство отсутствия сетевого вызова при `CERT_VALID` (лог), результаты unit-тестов x86/x64, хэши `bin\x86\l4pin.exe` и `bin\x64\l4pin.exe`. Отметь любые расхождения с контрактом 4.1 overview — они правятся только в overview с согласованием, не в коде молча.
