# PROMPT AGENT — шаг 5 / задание 6: документация инженера и сквозная стендовая проверка Этапа 1 на реальной машине

Ты Technical Writer + QA-инженер по Windows-терминалам: Markdown, PowerShell 5.1, чтение C-кода для сверки фактов. Разрешенные файлы: `tools/dist/README.md`, `tools/USER_GUIDE.md`, `docs/term_tool-user-guide.md` (создать, если нет; имя — по `docs/etran_dev-documentation-naming-convention.md`), `docs/README.md` (строка индекса), новый `tools/release/Test-Stage1Stand.ps1` (скрипт снимков «до/после», без установки), `prompts/prompt_step5_stacks_overview.md` (только раздел 1 «Статус» и раздел 7 «Что вернуть» — результаты). **Не менять** код C, скрипты сборки/публикации, серверные конфиги.

Прочитай [общий overview](prompt_step5_stacks_overview.md) полностью — особенно §4 (контракты, они и есть источник для документации), §5 (протокол стенда), §6 (владелец). План: `docs/term_tool-zero-touch-installer-and-remote-runtime-plan.md` §3.2 (S1–S10), §4.1, §6.2 (DoD 1–6, 8, 9), §7.4 («Первичная установка инженером»), §8.

Предусловие: задания 1–5 завершены и их отчеты доступны (версии, хэши, URL в реестре). Если что-то не завершено — документировать фактическое состояние с пометкой «не реализовано в Этапе 1», не описывать желаемое как существующее.

## 1. Факты

- Старое руководство: `tools/USER_GUIDE.md` (~24 КБ), `tools/dist/README.md` (~12.5 КБ), `C:\l4tools\terminal-tools-user-guide.md` на стенде — описывают трехфайловый дистрибутив (`l4install_x64.exe` + `tools.zip` + `ffmpeg.zip`) и ручной `l4pin.exe <PIN>`.
- Новый сценарий: один `l4setup.exe` по URL `https://l4tools-generic.ar.cloud.ru/l4tools/<semver>/l4setup.exe`, сверка `SHA256SUMS` (`certutil -hashfile l4setup.exe SHA256`), запуск `l4setup.exe [--pin <PIN>]`; если валидный сертификат уже стоит — PIN не нужен и не спрашивается; если сертификата нет и PIN недоступен — «Пропустить — ввести позже» / `--no-pin`, потом `l4setup.exe --pin <PIN>` или `l4pin.exe <PIN>` в любое время.
- Стенд: эта Windows x64 машина, `C:\l4tools`, службы `Leo4Proxy`, `mosquitto`, `L4Con`, `L4Superv`, сертификат `CN=a4b0000773c82116d210826`, thumbprint `CC88419A4C3763150A4C0905261EC073C58CFC09`, `NotAfter 2027-08-29`, ключ неэкспортируемый → S10 на стенде только по согласию владельца.
- Коды возврата, флаги, форматы `install_summary.json`/`pending_pin.json`/`l4tools-release.json` — строго из §4 overview (если реализация отклонилась — сначала исправляется overview заданием-владельцем, затем документация).

## 2. Задачи

1. **`docs/term_tool-user-guide.md`** (руководство инженера, целевой процесс, RU): «Скачать → проверить хэш → запустить → прочитать итог». Разделы: получение дистрибутива (URL реестра, `SHA256SUMS`, что делать при несовпадении), запуск (двойной клик / `--silent` / `--pin`), **таблица сценариев по состояниям S1–S10** с ожидаемым поведением и кодом возврата (особо: S9 «сертификат уже есть — ничего вводить не нужно», S10 «PIN позже», S7 «без сети»), чтение `install_summary.json` и `l4setup.log`, повторный запуск/`--repair`/`--smoke-only`, откат (`C:\l4tools\rollback\`), FAQ (SmartScreen на неподписанном exe; UAC; «терминал offline в MenuBuilder после установки»; «хочу перевыпустить сертификат» → `--force-reissue` и последствия), что **не** делать (не запускать `l4pin.exe --force` без необходимости, не удалять сертификат вручную). Явно указать: в Этапе 1 самообновления нет, новую версию инженер ставит тем же способом.
2. **`tools/dist/README.md`**: переписать под новый состав каталога (`l4setup.exe`, `l4tools-release.json`, `SHA256SUMS` — не в Git, берутся из реестра), ссылка на руководство, раздел «Сборка» (из задания 4) сохранить, легаси-раздел про `l4install_*`/`tools.zip` — короткий «переходный период, снимается в следующем релизе». `tools/USER_GUIDE.md` — либо привести к ссылке на `docs/term_tool-user-guide.md` с кратким quick-start, либо обновить целиком; дубли фактов исключить.
3. **`tools/release/Test-Stage1Stand.ps1`** (PowerShell 5.1, без установки чего-либо): `-Phase Before|After|Compare`. `Before`/`After` пишут JSON-снимок в `C:\l4tools\stand-<phase>-<timestamp>.json`: сертификаты `Cert:\LocalMachine\My` с issuer `*iot.leo4.ru*` (thumbprint, NotAfter, HasPrivateKey), ответ `http://127.0.0.1:18443/_leo4/info` (без секретов — там их нет), статус/путь 4 служб, `LastWriteTime` и SHA-256 всех `*.exe` под `C:\l4tools`, `state.json` целиком, наличие `pending_pin.json` (только факт), содержимое `install_summary.json`. `Compare` печатает таблицу PASS/FAIL по критериям DoD 8: thumbprint неизменен, `status ready`, службы `Running`, `cert.reused == true`, `exit_code == 0`, и по идемпотентности (хэши exe не изменились между двумя `After`). Скрипт только читает.
4. **Стендовый прогон по §5 overview** с записью результатов (каждый пункт — команда, ожидание, факт, PASS/FAIL, время):
   1. **[владелец]** подтверждение перезапуска служб → `Test-Stage1Stand.ps1 -Phase Before` + бэкап `state.json`, `l4superv.json` в `C:\l4tools\backup-<date>\`.
   2. Скачать `l4setup.exe` **из реестра** (`curl.exe -O https://l4tools-generic.ar.cloud.ru/l4tools/<v>/l4setup.exe` и `SHA256SUMS`), `certutil -hashfile` = `SHA256SUMS` = `HEAD digest`.
   3. `l4setup.exe --silent` → код `0`; `-Phase After`; `-Phase Compare` → все PASS; thumbprint `CC88419A…` неизменен.
   4. `l4setup.exe --silent --pin 000000` → код `0`, `warnings: cert_reused_pin_ignored`, в `l4setup.log` нет вызова `l4pin` с PIN и нет `function=check`.
   5. `l4setup.exe --silent` повторно → код `0`, хэши exe не изменились (идемпотентность).
   6. `l4setup.exe --smoke-only` → `0`/`12`, summary обновлен.
   7. **[владелец]** MenuBuilder UI: `online`, трансляция ≤ 1 с, удаленный ввод; остановить трансляцию. Зафиксировать ответ владельца дословно.
   8. `l4pin.exe 000000` (без `--force`) → код `0`, «reissue not required», thumbprint неизменен.
   9. `l4superv.exe --tick` → в логе немедленный такт; `state.json.installed_version` == версия релиза.
   10. **S10/S7 — только при явном «да» владельца** (новый PIN, согласие на `--force-reissue` на стенде) либо на ВМ: `l4setup.exe --silent --no-pin` на машине без сертификата → `10`, `standby_waiting_pin`, службы `Running`, `_leo4/info.status == standby`; затем `l4setup.exe --pin <PIN>` → `active` ≤ 15 с, `online`; либо создать `pending_pin.json` через `l4setup.exe --pin <PIN>` при отключенной сети → `11`, включить сеть → `l4superv` сам активирует. Без согласия — пометить «не выполнено на реальном стенде, покрыто unit/компонентными тестами заданий 2–3».
   11. Негатив без риска: отмена UAC → `20`; занятый порт 1883 чужим процессом (с согласия владельца) → `22`, процесс не убит.
5. **Отчет владельцу** (`prompts/prompt_step5_stacks_overview.md` §1 статус + §7 результаты, либо отдельный `prompts/step5_stage1_stand_report.md`): таблица DoD 1–6, 8, 9 со статусом и доказательством (ссылка на снимки/summary без PIN), список долгов Этапа 1 (Authenticode, S10 на реальном стенде, Win7 x86 стенд, `git rm --cached` бинарников — выполнено/ожидает), фактический код ответа реестра на перезапись (из задания 5), готовность к Этапу 2.
6. `docs/README.md`: добавить строку для `term_tool-user-guide.md`, обновить описание плана (Ревизия 4). Соблюсти `docs/etran_dev-documentation-naming-convention.md`.

## 3. Проверки

- Все команды в руководстве выполнены на стенде буквально (copy-paste) и дают описанный результат; коды возврата, флаги и поля JSON сверены с реальными бинарниками заданий 1–3 (`--version`, `--help`), а не только с overview.
- Ссылки в Markdown валидны (относительные пути существуют); нет секретов, PIN, ключей реестра, приватных путей `d:\.ssh`.
- `Test-Stage1Stand.ps1` запускается под `-ExecutionPolicy Bypass`, ничего не изменяет (проверить: хэши файлов до/после запуска скрипта совпадают), `Compare` корректно показывает FAIL на искусственно измененном снимке (подменить thumbprint в JSON вручную).
- Изменения только в `docs/`, `tools/*.md`, `tools/release/*.ps1`, `prompts/` — тесты/линтеры Python-проектов по правилам репозитория не запускаются.

## 4. Результат

Верни: ссылки на обновленные документы, `Test-Stage1Stand.ps1`, снимки `Before/After/Compare` (без секретов), таблицу прогона §2.4 с PASS/FAIL и временем, дословные подтверждения владельца по пунктам 1, 7, 10, 11, итоговую таблицу DoD и список долгов. Если реализация заданий 1–5 расходится с overview — перечислить расхождения отдельно, не «сглаживать» в документации.
