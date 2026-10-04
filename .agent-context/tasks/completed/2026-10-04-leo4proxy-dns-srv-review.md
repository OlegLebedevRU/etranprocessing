# Handoff: leo4proxy DNS/SRV review

## Контекст задачи

- Scope: проверка концепции и подготовка контекста; внедрение ожидает подтверждения.
- Ревизия: `c58f607`, worktree, без commit; дата 2026-10-04.
- Владельцы: ProcessingBackend policy, leo4proxy transport, l4setup install,
  l4superv restart/repair, инфраструктура DNS/CA.
- Intake и границы: [packet](../../../docs/term_net-leo4proxy-dns-srv-implementation-context.md).

## Выполнено

Подготовлены packet, компонентная карточка и навигация. В packet перечислены
расхождения концепции с кодом, предложения для согласования и acceptance matrix.
По уточнению пользователя план обновлён: последний policy bootstrap по публичному
IP, per-channel fallback_ips, TTL и отдельные cold/warm no-DNS сценарии.
Это план, IP/SNI/runtime не проверялись и внедрение не начиналось.
Оценён дополнительный вариант unverified IP fallback: route hijack/MITM может
подменить policy. Предложен strict default и SPKI emergency; полный unverified
режим/независимая подпись требуют выбора до реализации, не включены автоматически.

## Затронутые контракты

| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| policy optional endpoints | ProcessingBackend c58f607 | leo4proxy 1.7.3 | существующий parser игнорирует семантику unknown fields, но валидирует весь JSON; тест old/new ещё нужен |
| proposal policy IP bootstrap | provisioned install profile + policy API | leo4proxy policy worker/channels | fallback_ips/TTL optional v=1; logical TLS identity сохраняется, runtime pending |
| SCM/config args | l4setup + suite installer | SCM/l4superv | сохранение custom args не реализовано как описано концепцией |
| local info endpoints | leo4proxy | l4superv | новое поле и consumer parser ещё не реализованы |

## Изменённые инварианты

Нет. Изменение TLS/fallback режима лишь предложено, ожидает согласования.

## Проверено

- [x] Концепция прочитана; версия SHA-256 зафиксирована в packet.
- [x] Статически сверены producer policy и C consumer, TLS/HTTP и service install paths.
- [x] Сверены первичные SDK/RFC по DNS deadline, trust root, TLS handshake и SRV.
- [x] Проверены локальные Markdown links и `git diff --check`, exit code 0.
- [ ] Backend/native tests/builds — doc-only задача, не запускались.
- [ ] CA/DNS/runtime/packaging/E2E — не выполнялись, не выдавать за evidence.

## Наблюдаемое evidence

Уровень документ + статический код; первичные ссылки и выводы в packet.
Credentials/PIN/JWT/keys не читались и не сохранялись. Сессии/процессы не создавались.

## Риски и следующие действия

После подтверждения согласовать TLS/fallback/bootstrap решения и выполнить шаги
packet. Production rollout требует отдельной release-задачи и beta evidence.
Cleanup: временные артефакты и test credentials не создавались.

## Обновить context distillates

- [leo4proxy](../../components/leo4proxy.md): создана; статическая сверка c58f607.
- [ProcessingBackend](../../components/processing-backend.md): добавлено уточнение
  актуального policy producer и реализованного C consumer, статический код c58f607.
