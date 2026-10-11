# JWT signer и native CAPTCHA

## Контекст задачи / intake
- Владелец JWT/auth: MenuBuilder. Producer: backend identity/session → HMAC HTTP
  → выделенный signer → RS256 → backend и nginx. Миграции БД не требуются.
- Дополнительная задача пользователя: план native CAPTCHA из внешнего файла;
  MenuBuilder, L4Desk landing, Platerra relay/UI; Leo4 только CTA/deep-link.
- Исходная main: e72c046b3ae708230065fdfbd1e42423765c69cc.
- Закрытый RSA уже размещён оператором вне repo/образа: root-only directory,
  UID10001 file0400. Нет passphrase; пара проверена по DER SHA256 публичного ключа.
- Serverless сохранён, но недоступен и выключен до восстановления на новом ключе.
- [MCP Ops Readiness: UNAVAILABLE]; SSH с проверкой host key работает на обоих
  разрешённых хостах. Временные SSH копии подготовлены оператором вне checkout.
- Инварианты: отсутствующие секреты fail closed; нет публичного signer-порта;
  tenant/session claims не объединяются; org claims string → auth boundary int.
- Запрещённые каталоги legacy/tools/SQL не затрагиваются. Platerra — отдельный
  чистый checkout внутри временной рабочей папки; исходная копия не изменяется.

## Выполнено
- Приватный signer, новая пара RS256 public consumers, внутренний приоритет,
  отдельные HMAC secrets и сохранённый external reserve; response validation.
- Native challenge v1 (HMAC/600s), три provider modes, UI и relay dispatch;
  секреты env-only. Stateless/replay ограничения задокументированы.
- Первый activation helper с rollback; последующие releases обновляют оба
  MenuBuilder процесса одним digest и откатывают оба при ошибке.

## Проверено
- [x] Полный MenuBuilder backend: 748 passed, 22 skipped, 52 прежних warnings.
- [x] Ruff/Pyright app прошли; после изменения no-store route повтор обязательных
  checks выполняется перед commit.
- [x] CI release unit suite: 33 tests; paired rollback включён.
- [x] L4Desk relay: 11 tests; Platerra relay: 13 tests.
- [x] Platerra Node build/check; L4Desk JavaScript syntax.
- [x] Production key pair SHA256 DER:
  1048fafba06c30ab4fb6efee944dc64c2df413d335c2f7fa46d5a5daac17018f.
- [ ] Frontend build/Vitest/browser — Windows ACL исправляет оператор.
- [ ] Secret scan окончательного набора, commit, registry artifact и runtime release.
- [ ] Login/refresh/tenant switch с approved short-lived credentials.
  Из production signing secrets тестовые JWT не генерировать.
- [ ] Реальная email доставка: в задаче не выполнялась; positive tests используют mocks.

## Release / риски / cleanup
- Release выполняется совместным шагом после обеих реализаций по указанию пользователя.
- MenuBuilder → builder/registry → production; сайты → существующий Compose на176.
- Старые access JWT перестанут приниматься; refresh sessions сохраняются.
- Native CAPTCHA слабее SmartCaptcha; rollback provider требует пересоздания
  relay/backend для перечитывания env. Yandex settings сохраняются.
- Временные SSH копии за пределами write scope удаляет оператор после release.
- Ephemeral тестовые ключи и временные checkout внутри .release-test-tmp подлежат
  адресной очистке после сохранения результата; чужие untracked файлы не удалять.
