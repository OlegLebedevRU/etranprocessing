# L4Release — этап 1, candidate 1.13.2

## Scope / ownership

- Владелец: tools release. Единый Python3.14 uv build/sign/setup/publish.
- Producer → consumer: config/env → runner → native build / PS signing / Registry.
- Worker/layout/RPC/catalog не реализованы; PB/MB/shared/IoT runtime не менялся.
- Основа: [принятая архитектура](../../../docs/term_arch-l4update-flow.md).

## Результат

- [l4release](../../../l4release/__main__.py), root uv project/lockfile,
  declarative config и тонкий build_dist wrapper.
- prepare / release / verify / keys init / locked OpenH264 import.
- Native x86/x64/default, fresh PE checks, setup tests обеих архитектур.
- Global lock, input/output hashes, phase checkpoints, bounded commands,
  redacted logs; signing resume без пересборки компонентов.
- Owner-approved automatic Authenticode/RFC3161, строгая проверка payload,
  immutable/idempotent publication через существующий publisher.
- Candidate 1.13.2 опубликован из чистого source checkpoint:
  `20033b2045b0e09b86d3c04ec93feac34a197edf`.
- [l4setup.exe](https://l4tools-generic.ar.cloud.ru/l4tools/1.13.2/l4setup.exe):
  29,783,096 bytes,
  SHA256 `cea43ba5383cb899e9d56cbd3b6faf23aad5306cf8ddc649c5d1b8b88a58b60e`.
- [Manifest](https://l4tools-generic.ar.cloud.ru/l4tools/1.13.2/l4tools-release.json),
  [SHA256SUMS](https://l4tools-generic.ar.cloud.ru/l4tools/1.13.2/SHA256SUMS).

## Проверки

- 42 release safety/checkpoint/key/dependency/publisher tests PASS.
- Ruff check/format, pyright PASS; changed-source credential scan PASS.
- Все семь native tools build/gates PASS; l4capture 129/129.
- l4setup tests x86/x64 PASS; setup built x86/x64/default.
- Проверены 19 подписанных EXE и 61 embedded file для каждой архитектуры.
- Registry upload + HEAD digest PASS; full GET hashes/sizes PASS.
- Отдельно все три файла полностью скачаны без Authorization: PASS.
  Существующий publisher делает свой GET с ключом; anonymous check был отдельным.
- Первый publish получил read timeout после загрузки файлов. Повтор release
  использовал signed checkpoint, сверил существующие байты, не загружал повторно.
- До publish успешные команды заняли 352.290s (~6 минут без дополнительных I/O).
  Первый publish 240.395s/timeout; retry verify 4.191s + publish 9.819s.
- Evidence: ignored tools/dist/.release/1.13.2/report.json, logs/, public-read.json;
  durable publication record artifacts/l4tools/1.13.2.json и releases.jsonl.

## Исправленные проблемы окружения

- Изолированный codexsandboxonline: persisted CNG 0x80070002, GDI BitBlt error5,
  restricted-token/ACL fixtures denied. Пользователь подтвердил l4pin COMPLETE
  и l4capture 129/129 в обычной сессии.
- Full access вернул профиль oleg_; автоматические OS gates прошли.
- Windows PS5 наследовал PSModulePath от pwsh7, ломая Authenticode module load.
  Runner очищает эту переменную только для PS5 child; regression test PASS.
- OpenH264 2.6.0.2502 восстановлен из прежнего l4capture-debug vendor:
  четыре headers/шесть /MT static libraries pinned SHA256 в openh264.lock.json.
  Import проверяет все источники до копирования и не заменяет отличающиеся файлы.

## Не выполнено / следующий этап

- Не было установки на 773, изменения служб/SCM/MQTT или backend deploy.
- Stable admission / mixed-version communication gate / terminal gate / promote
  пока отсутствуют и явно отклоняются, не подменяются успешной публикацией.
- OpenH264 source revision/rebuild recipe не аттестованы: pinned готовые assets.
- Metadata RSA3072 key creation реализовано, реальный ключ не создавался;
  каталог и detached metadata signatures относятся к следующему этапу.
- Далее этап 2: Program Files/ProgramData layout и подготовка l4setup по архитектуре.
