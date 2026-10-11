# JWT signer и native CAPTCHA — выпуск 2026-10-11

## Контекст и инварианты
- Владелец auth: MenuBuilder. Identity/session → HMAC HTTP → отдельный signer → RS256 → backend/nginx. Миграции БД не требуются.
- Выполнен пользовательский план native CAPTCHA для MenuBuilder, L4Desk и Platerra; Leo4 CTA/deep-link сохранён.
- Закрытый RSA: каталог root:root0700, файл10001:10001/0400; без passphrase, вне Git и образов. Отсутствующие секреты fail closed; signer без публичных портов; tenant/session claims изолированы.
- Serverless сохранён и выключен: функция/старый ключ недоступны. Включение допустимо после восстановления с новой парой ключей.
- [MCP Ops Readiness: UNAVAILABLE]; штатный SSH с проверкой host keys. Запрещённые каталоги legacy/tools/SQL не исследовались. Исходный внешний Platerra checkout не изменялся.

## Проверки реализации
- [x] MenuBuilder backend748 passed/22 skipped, Ruff check/format и Pyright; CI release34 tests, paired rollback и Python3.12 AST regression.
- [x] Frontend build, 88 Vitest tests, 4 Playwright auth scenarios.
- [x] L4Desk relay11 и Platerra relay13 tests; Platerra build/check; L4Desk JS syntax; mocked contact browser smoke обоих сайтов и Leo4 topic.
- [x] Secret scan35 task files: private keys/credentials не обнаружены; fixture secrets проверены.
- Native challenge v1 HMAC/600s, provider native/smartcaptcha/off, секреты env-only; stateless replay в пределах TTL возможен, native слабее SmartCaptcha.

## Выпуск и runtime evidence
- MenuBuilder backend/signer/frontend: опубликованный45e69a793e6c316750e286809a6a85acd8cb6989, штатный builder → registry → production. Первый builder run выявил несовместимый с Python3.12 except syntax; исправлено scoped Ruff targetpy312 и AST test до production.
- Backend/signer digest: sha256:bdf5bb1c102baad6a07e643c18ff872144cd442d9754d8d4621519ace97f511f.
- Frontend digest: sha256:6f0ec876c9079ecfdda92d9c2f4465cdb43259f3d46a0e9f990b4ae04500beab; static delivery проверена.
- Signer Healthy: UID10001:10001, read-only, cap_drop ALL, no-new-privileges, только приватная сеть, без host ports. Backend/signer имеют одинаковый image ID и internal HMAC secret; mock/fallback выключены. Health200, неверный HMAC401.
- DER public SHA256 пары и backend/nginx: 1048fafba06c30ab4fb6efee944dc64c2df413d335c2f7fa46d5a5daac17018f. Права закрытого ключа сохранены.
- Platerra website/contact: опубликованный58d8939c1940c2b63191899e05be5f621ec00c29; L4Desk contact45e69a7.
- Live acceptance выявил общий Docker DNS alias contact. Исправлен уникальным l4desk-contact в опубликованных etran03b976880c8042f547a927d9318a61f092447831 и Platerra30f973ba865cbd9af316d569e3978e79c8182c6b.
- L4Desk landing из03b9768: image ID sha256:2c6f3519020ed412d1040e25847fb825d383c20348a52dc4954e874f0a9d9831; shared edge config30f973b применён. Contact code при DNS correction не менялся.
- [x] После correction: четыре site containers Healthy, nginx -t/reload успешны; HTTPS health200 на L4Desk, Platerra, Leo4 с проверкой сертификатов.
- [x] Обе публичные формы: native challenge200 + Cache-Control no-store, неверный ответ400; MenuBuilder CAPTCHA native200, отсутствие CAPTCHA400. Email не отправлялся.
- Leo4/соседние builder backend-контейнеры и production nginx-default/прочие сервисы сохранили ID.
- Защищённые rollback backups и runtime secrets оставлены на серверах. Временные материалы очищаются адресно; чужие untracked файлы сохраняются.

## Невыполненные E2E и ограничения
- [ ] Login/refresh/tenant switch с approved short-lived credentials: credentials не предоставлены; JWT из production private key не создавались.
- [ ] Реальная email доставка: не выполнялась; positive tests используют mocks.
- Старые access JWT после смены ключа больше не принимаются; refresh sessions сохраняются. SmartCaptcha settings сохранены; переключение provider требует пересоздания backend/relay.

## Cleanup после выпуска
- Удалён task staging на обоих серверах; защищённые rollback backups и runtime secrets сохранены.
- Основная рабочая main синхронизирована с опубликованным c856a96 после проверки всех37 task files по Git blob hashes; пользовательские untracked файлы сохранены.
- Автоматическая проверка исполнения отклонила локальный Remove-Item как blocked by policy. Локальные временные каталоги и обе временные SSH-копии остались; исходные SSH-ключи не изменены.
- Оператору удалить только следующие точные цели после проверки, что release checkouts чистые и их HEAD опубликован:
  - `D:\.ssh\id_ed25519_codex_jwt_task`
  - `D:\.ssh\free-tier-cloud_ru_codex_jwt_task`
  - `D:\repo\platerra\Public\etranprocessing\.release-test-tmp\jwt-signer`
  - `D:\repo\platerra\Public\etranprocessing\.release-test-tmp\etran-jwt-native-release`
  - `D:\repo\platerra\Public\etranprocessing\.release-test-tmp\platerra-native`
  - `D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\.pytest_tmp_jwt_signer`
  - `D:\repo\platerra\Public\etranprocessing\MenuBuilder\backend\.pytest_tmp_jwt_signer_full`
- Нельзя удалять общий .release-test-tmp или применять git clean: чужие временные/untracked материалы не входят в эту задачу. Старые pytest-каталоги также ограничены Windows ACL.
