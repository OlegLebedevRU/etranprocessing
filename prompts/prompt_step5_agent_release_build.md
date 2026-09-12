# PROMPT AGENT — шаг 5 / задание 4: `tools/build_dist.cmd` — сборка релиза `l4tools` (payload x86/x64 в ресурсах, `l4tools-release.json`, `SHA256SUMS`, версия, вывод бинарников из Git)

Ты Build/Release инженер под Windows: cmd/PowerShell 5.1, MSVC Build Tools 2022 (`VsDevCmd.bat`, `rc.exe`, `link.exe`), Git. Разрешенные файлы: `tools/build_dist.cmd`, `tools/build_dist_win7.cmd`, `tools/build_dist_win7_sp1.cmd`, новый `tools/release/` (PowerShell-скрипты манифеста), `tools/version.txt` (единая версия), `tools/l4setup/res/version.h` (генерируется), `.gitignore` (корень или `tools/`), `tools/dist/README.md` — только раздел «Сборка» (текст для инженера пишет задание 6). **Не менять** исходники C заданий 1–3, `deploy/`, `.github/`.

Прочитай [общий overview](prompt_step5_stacks_overview.md): раздел 2.1 (текущий `build_dist.cmd` → `l4superv/build.cmd all` → `pack_zip.cmd`), 4.5 (контракт релиза — обязателен), 6 (владелец: подтверждение удаления бинарников из Git). План: Пакет 1 п. 4, Пакет 5 п. 1–2, 4; раздел 7.4 (раскладка в реестре).

## 1. Факты

- Сейчас `tools/build_dist.cmd` вызывает `tools/l4superv/build.cmd all` и `pack_zip.cmd`, результат — `tools/dist/{tools.zip (1.5 МБ), ffmpeg.zip (28.6 МБ), ffmpeg.zip.sha256, l4install_x86.exe, l4install_x64.exe, l4install_*.cmd, README.md}` и `tools/dist_win7_sp1/…` — всё закоммичено в Git.
- Сборка компонентов: `tools/l4pin/build.cmd [all|x86|x64]`, `tools/l4superv/build.cmd`, `tools/l4desk/build.cmd all`, `l4con`, `l4sql`, `leo4proxy` — свои `build.cmd`; `ffmpeg` — готовые бинарники в `tools/ffmpeg`/`ffmpeg-win32` (сверить, откуда берет `pack_zip.cmd`). Версии компонентов: `leo4proxy` 1.2.0, `l4desk` 1.5.0 (из `_leo4/info` и Шага 4), остальные — из `res/*.rc` или `CHANGELOG.md`.
- `l4setup.exe` (задание 2) ожидает ресурсы `RT_RCDATA` с именами `PAYLOAD_X86` и `PAYLOAD_X64` (zip-архивы, распаковываемые miniz) и `version.h` с `L4TOOLS_VERSION "1.6.0"` + числовой формой для `VERSIONINFO`.
- Билдер Linux (`176.108.247.249`) MSVC не собирает: итог этого задания — локально собранный релиз-каталог, который задание 5 переносит на builder и публикует. `git_sha` в манифесте должен соответствовать коммиту, из которого собран релиз (проверять чистоту рабочего дерева — `git status --porcelain` пуст, иначе `dirty` в манифесте и отказ публиковать `stable`).

## 2. Задачи

1. **Единая версия:** `tools/version.txt` (одна строка SemVer, напр. `1.6.0`). `build_dist.cmd` читает ее, генерирует `tools/l4setup/res/version.h` (`#define L4TOOLS_VERSION "1.6.0"`, `L4TOOLS_VERSION_RC 1,6,0,0`) и передает `/DL4TOOLS_VERSION` в сборки `l4superv`/`l4pin`, если их `build.cmd` это поддерживают (иначе — зафиксировать как долг, не ломать их сборку).
2. **Порядок сборки в `build_dist.cmd`:** `l4pin` → `l4superv` → `l4desk` → `l4con` → `l4sql` → `leo4proxy` (каждый `build.cmd all`; провал → выход `1` с именем компонента) → формирование двух staging-каталогов `tools/dist/.stage/x86` и `x64` со структурой, идентичной `C:\l4tools` (`l4pin/ l4superv/ l4desk/ l4con/ l4sql/ leo4proxy/ mosquitto/ ffmpeg/`, плюс `terminal-tools-user-guide.md`, `example_mosquitto.conf`, `l4superv.json` по умолчанию). Правило подбора: x86-ветка получает 32-битные бинарники всех компонентов и 32-битный FFmpeg (набор `dist_win7_sp1`), x64 — 64-битные.
3. **Payload:** упаковать staging в `payload_x86.bin`, `payload_x64.bin` (zip deflate, `Compress-Archive` даёт zip без проблем для miniz; проверить, что miniz в `l4setup` читает zip64 — при > 4 ГБ не актуально). Сгенерировать `tools/l4setup/res/payload.rc` (`PAYLOAD_X86 RCDATA "…\payload_x86.bin"`, `PAYLOAD_X64 RCDATA "…"`) и вызвать `tools/l4setup/build.cmd` — итог `tools/dist/l4setup.exe`.
4. **Манифест `l4tools-release.json`** по 4.5 overview (PowerShell-скрипт `tools/release/New-ReleaseManifest.ps1`): `version`, `git_sha` (`git rev-parse HEAD`), `dirty` (bool), `built_at` (UTC ISO 8601), `builder: "windows-dev"`, `files.l4setup.exe.{sha256,size}`, `components{}` (версии из `VERSIONINFO` каждого exe через `[System.Diagnostics.FileVersionInfo]`), `payload_sha256.{x86,x64}`, `min_os: "6.1"`, `arch: ["x86","x64"]`. Плюс `SHA256SUMS` в формате `sha256sum` (`<hex>  l4setup.exe`, `<hex>  l4tools-release.json`), чтобы на builder работал `sha256sum -c SHA256SUMS`.
5. **Проверка результата в скрипте:** `l4setup.exe --version` печатает ровно `version.txt`; размер payload > 20 МБ каждый (иначе ffmpeg не попал); `Get-AuthenticodeSignature` → `NotSigned` — записать в манифест `signed: false` (Authenticode — долг Этапа 1). Ресурсы `PAYLOAD_X86/X64` присутствуют (проверить любым способом, напр. через `[System.Reflection]`-independent разбор — достаточно собственного `l4setup.exe --list-payload`, если задание 2 его добавит; иначе — сверка размера exe ≈ сумма payload + bootstrapper).
6. **Вывод бинарников из Git (Пакет 5 п. 4):** добавить в `.gitignore`: `tools/dist/*.exe`, `tools/dist/*.zip`, `tools/dist/*.bin`, `tools/dist/*.sha256`, `tools/dist/.stage/`, `tools/dist/l4tools-release.json`, `tools/dist/SHA256SUMS`, `tools/dist_win7_sp1/*` кроме `README.md`, `tools/l4setup/res/payload.rc`, `tools/l4setup/res/version.h`. **Удаление уже отслеживаемых файлов (`git rm --cached`) выполнить только после подтверждения владельца** (overview, раздел 6) и после публикации `1.6.0` в реестре заданием 5 — до этого только `.gitignore` + запись в отчете. Историю Git не переписывать.
7. **Совместимость:** старые `l4install_*.exe` и `.cmd` не собирать по умолчанию; оставить `build_dist.cmd legacy` для их сборки на переходный период (один релиз).
8. Обновить раздел «Сборка» в `tools/dist/README.md` (команда, где искать результат, что коммитится, а что нет).

## 3. Проверки

- Запуск `cmd /c tools\build_dist.cmd` на этой машине (MSVC найден по путям из `l4superv/build.cmd`): завершение `0`, в `tools/dist/` ровно `l4setup.exe`, `l4tools-release.json`, `SHA256SUMS`, `README.md`, `.cmd`-обертки; `.stage/` удален или в `.gitignore`.
- `l4setup.exe --version` == `version.txt`; `certutil -hashfile tools\dist\l4setup.exe SHA256` == `files.l4setup.exe.sha256` в манифесте == строка в `SHA256SUMS`.
- Повторная сборка без изменений: манифест отличается только `built_at` (детерминизм payload не требуется, но sha payload фиксировать).
- `git status` после сборки: не появилось неотслеживаемых бинарников (кроме тех, что временно остаются до `git rm --cached`).
- Ручная распаковка `payload_x64.bin` (`Expand-Archive`) → структура совпадает с `C:\l4tools` стенда (список каталогов), `ffmpeg\ffmpeg.exe -version` запускается.
- Совместно с заданием 2 (если готово): `tools\dist\l4setup.exe --silent` на стенде по протоколу overview §5 п. 2 — код `0`, `cert.reused`, thumbprint без изменений.

## 4. Результат

Верни: итоговый `build_dist.cmd` и `New-ReleaseManifest.ps1`, `l4tools-release.json` и `SHA256SUMS` тестовой сборки, лог сборки (усеченный), список файлов, оставшихся под Git до подтверждения владельца, известные долги (Authenticode, Windows-раннер, `/D`-версия для компонентов, если не поддержана).
