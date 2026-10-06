# L4Release — этап 1

## Intake

- Цель: первый принятый этап L4Update, единый uv build/sign/setup/publish интерфейс.
- Владелец: tools release; API, native runtime и backends не меняются.
- Producer→consumer: config/env → Python runner → существующие build/sign/publisher.
- Инварианты: clean source для signed release, x86/x64/default, timestamps/payload,
  отсутствие секретов в логах, один workspace writer, immutable Registry publication.
- Источник: [архитектура](../../../docs/term_arch-l4update-flow.md).
- Рабочее дерево без commit; исходный HEAD a39a6521.

## Реализовано

- Root Python3.14 uv project/lockfile, [module](../../../l4release/__main__.py),
  declarative build config и замена обычной build_dist orchestration тонким wrapper.
- prepare без подписи; release с автоматической подписью существующим PS,
  проверкой каждого EXE/двух embedded payload и существующей строгой публикацией.
- Phase checkpoints/hash inputs, signing resume, global lock, redacted stage logs,
  failures/actual duration в локальном ignored report.
- keys init: encrypted RSA3072/PKCS8 + public export/self-test, без перезаписи.
- Env path явный; значения рабочего sw_sign.env не выводились/не копировались.
  IOT key не передаётся build children.
- Руководство/changelog/skill обновлены на согласованный automatic signing.
- Live terminal gate/promotion пока fail-closed, не скрытые stubs успеха.

## Проверки и ограничения

- 42 safety/checkpoint/key/CLI/dependency/publisher tests passed;
  ruff check/format и pyright: без ошибок.
- PowerShell syntax Prepare-BuildVersion/Verify-SignedRelease проверен AST parser.
- OpenH264 2.6.0.2502 восстановлен из существующего l4capture-debug vendor;
  4 headers + 6 static libraries зафиксированы SHA-256 в openh264.lock.json.
  Import проверяет все файлы до копирования; preflight проверяет lock.
  Source rebuild/revision не аттестованы: это восстановление готовой зависимости.
- l4capture build.cmd all: x86/x64 PASS. test.cmd: 127 passed / 2 failed,
  GDI capture rc=3 в этом окружении; DXGI unsupported cases отмечены самим runner.
- Full prepare 1.13.1 остановился на l4pin CNG test key 0x80070002:
  version 1.277s, l4pin 22.783s; лог tools/dist/.release/1.13.1/logs/l4pin.log.
  Причина отказа CNG не доказана; существующий test gate не ослаблялся.
- Signing/publication не выполнялись; AR_GENERIC_KEY_ID/SECRET теперь заданы;
  release version не выпускалась. После прогона исходные tracked binaries восстановлены.
- Env W_SIGN_PFX_PASSWORD пуст; допустимость зависит от того, зашифрован ли PFX.
- Root dirty из разработки; production release требует чистого принятого checkpoint.
- Никаких вызовов IoT, MQTT, изменений SCM/служб/сертификата773 и deploy.
- PB/MB/shared tests N/A: их код не менялся.

## Следующий минимальный шаг

### Продолжение после добавления credentials

- Env проверен без вывода значений: AR credentials заданы, PFX существует и
  читается с пустым W_SIGN_PFX_PASSWORD.
- OS probes: CNG ephemeral key creation/finalization PASS; GDI BitBlt(1px)
  Win32=5 Access denied. Persisted-key gate в окружении агента не пройден.
- Оператор сообщил l4pin build.cmd all COMPLETE и l4capture test.cmd 129/129 PASS.
- Независимые diagnostics: l4desk/l4sql x86/x64 PASS; остальные gates остановились
  (restricted-token/ACL fixtures, file-manager directory access, proxy gate).
  Отчёт tools/dist/.release/diagnostics/report.json не даёт admission.
- Старый failed prepare report сохранён как report.failed-cng.json; completed
  checkpoints отсутствовали. Ожидается полный prepare в операторской сессии.

### Full access и автоматическая проверка

- После переключения профиль команд — oleg_, OS-dependent native gates проходят.
- Все семь tools собраны x86/x64/default и существующие native gates прошли;
  l4capture 129/129, setup tests x86/x64 PASS.
- Prepare 1.13.1 дошёл до unsigned manifest; обнаружен наследованный pwsh7
  PSModulePath в Windows PowerShell5. Runner теперь очищает эту переменную только
  для PS5 child; regression test PASS. Повторные manifest/payload проверки PASS.
- Diagnostic Authenticode signing + RFC3161 + signtool verification PASS.
  Это отдельный probe, не опубликованный релиз.
- Владелец выбрал candidate 1.13.2; Registry 1.13.1=200, 1.13.2=404 до выпуска.
- Changed-source credential scan PASS; PB/MB/shared не менялись.
- Следующий запуск: полный release 1.13.2 из чистого зафиксированного источника.

Проверить CNG и GDI native gates в полноценной операторской Windows-сессии,
подготовить AR write credentials в sw_sign.env и завершить полный native prepare.
После согласованного чистого source checkpoint проверить signing/publication нового
immutable candidate. Это не stable admission и не доказательство нового l4update.
Текущие файлы вне workspace не изменялись. Пользователь не знает путь vendor;
он найден read-only проверкой конкретного tools/l4capture пути registered worktrees.
