# L4 Tools 1.9.3: сборка и публикация

## Scope и источник
- Пользователь разрешил собрать и опубликовать новый l4tools; подпись PFX выполняет лично оператор.
- Изолированный checkout `etranprocessing-l4tools-193`, ветка `release/l4tools-1.9.3`, база `origin/main` = `c6096f4a3a0841ab2b19f717a4dbe4d58de7f24c`.
- Включены изменения leo4proxy 1.7.2 и l4con 1.9.3, их документация и тесты. Общая версия пакета/установщика — 1.9.3.
- Runtime установленных служб не изменялся при сборке. Backend/frontend проверки не применяются: их код не изменён.

## Контракт выпуска
- `Complete-SignedRelease.ps1` подписывает 18 EXE двух payload, перепаковывает payload, пересобирает и подписывает установщик, обновляет manifest и SHA256SUMS.
- Каждая подпись получает RFC 3161 timestamp (`/tr`, `/td SHA256`); default `http://timestamp.digicert.com`, допускается явный `-TimestampUrl`.
- Проверяется сертификат timestamp и `signtool verify /pa /all /tw`. Ошибка прекращает подготовку подписанного выпуска.
- PFX и пароль не сохраняются в Git. Публикация unsigned/dirty артефактов не разрешена.

## Проверено 2026-10-01
- [x] `tools/build_dist.cmd 1.9.3`: полный повторный проход x86/x64, exit 0. Первый проход завершился ошибкой отсутствия OpenH264 в новом checkout; ошибка устранена подключением существующих локальных заголовков и статических библиотек, затем сборка повторена целиком.
- [x] OpenH264: использованы подготовленные зависимости из checkout `etranprocessing-l4capture-debug`, без изменения того checkout; SHA-256 библиотек сохранены локально в ignored `tools/l4capture/obj/openh264-build-dependency-hashes.json`.
- [x] FFmpeg: исходные x86/x64 артефакты текущей рабочей копии; Mosquitto — существующий universal x86. Все собственные компоненты пересобраны.
- [x] leo4proxy `tests/test_policy.cmd`: x86/x64, exit 0.
- [x] l4con `tests/test_user_events.cmd`: x86/x64, exit 0.
- [x] l4pin: встроенные в build 7/7 in-memory certificate tests для каждой архитектуры.
- [x] l4setup `run_tests.cmd`: exit 0; временные тестовые каталоги, без установки пакета в боевую папку.
- [x] `Test-L4CapturePackage.ps1`: exit 0; stage x86/x64 и capture quality/license.
- [x] По 9 EXE в каждом stage; проверены PE machine, включая намеренный x86 Mosquitto в обоих payload. Версии l4con/leo4proxy/l4setup совпадают с 1.9.3/1.7.2/1.9.3.
- [x] Read-only secret scan: совпадения — существующие шаблоны команд и фиктивные ключи backend unit tests; новые credentials не добавлены.
- [x] PowerShell AST parsing signing scripts и `git diff --check`.
- [x] Registry version 1.9.3 до подписи: отсутствует (404).
- [x] Оператор выполнил подпись. Независимая проверка всех 19 EXE: Authenticode Valid, присутствует TimeStamperCertificate, `signtool verify /pa /all /tw` exit 0; один signing certificate.
- [x] Встроенные ресурсы PAYLOAD_X86/X64 извлечены через Win32 resource API: SHA-256 совпадает с manifest и ZIP файлами; все 18 встроенных EXE побайтно совпадают с подписанным stage.
- [x] Source snapshot `ac320caa518960094e717766129f09103f6e8504` опубликован в main, checkout был clean при генерации подписанного manifest (`dirty=false`).
- [x] Все три immutable файла 1.9.3 загружены с HTTP 200; registry HEAD digest совпадает. Две GET проверки штатным Python publisher завершились ошибкой неполного тела (28 964 588 и 27 581 627 вместо 29 585 464 байт); результат не объявлялся успешным, файлы не перезаписывались.
- [x] Независимый HTTPS GET через curl HTTP/1.1 завершён полностью: installer 29 585 464 байта, SHA256SUMS 165 байт, manifest 1 357 байт. Размер и SHA-256 каждого скачанного файла совпадают с локальными; downloaded installer Authenticode Valid с timestamp.
- [x] После полной проверки вызван штатный `publish_l4tools.py record tools/dist`: запись `artifacts/l4tools/1.9.3.json` и journal `releases.jsonl`, published_at `2026-10-01T12:09:09.379762+00:00`.

## Результат и границы
- Installer: `https://l4tools-generic.ar.cloud.ru/l4tools/1.9.3/l4setup.exe`.
- SHA-256: `a0c512355a2348230c35362d8f8f40ae6dd8c4f948190c078b00c99ff881c924`.
- Публикация завершена; установка общего пакета на терминал не выполнялась. Ранее проверенные отдельные leo4proxy/l4con в боевых папках сохранены.
- Причина неполных Python GET не установлена; отдельная доработка publisher не входила в задачу. Полнота именно этого выпуска подтверждена независимым скачиванием.
- Изолированный checkout с подписанными build artifacts сохранён. Автоматическая проверка запретила удаление временных контрольных downloads; они оставлены в `tools/dist/.verify` и исключены локально из Git. PFX/пароль не копировались и не сохранялись агентом.
