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
- [ ] Подпись оператором всех 19 EXE и независимая проверка timestamp/Authenticode.
- [ ] Публикация immutable 1.9.3 и проверка скачанных файлов, запись release metadata.

## Следующий шаг
Сначала сохранить проверенный source/build snapshot в базовой ветке и обновить manifest из clean checkout. Затем дать оператору команду `Complete-SignedRelease.ps1`. После его подтверждения независимо проверить все подписи, payload и SHA256, выполнить штатный `deploy/publish_l4tools.py` без `--allow-dirty`, записать результат. Изолированный checkout сохраняется до окончания подписи/публикации.
