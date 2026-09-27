# L4D-17F-DOCS-FIX-01 — финальная корректирующая black-box приёмка (v2)

```yaml
prompt_id: L4D-17F-DOCS-FIX-01
scope_project: l4desk-service
scope_root: D:\repo\platerra\Public\etranprocessing-l4tools-182\l4desk-service
prompt_type: corrective-black-box-acceptance
registration_id: R-L4D-17F-DOCS-FIX-01-v2
blocked_prompt_id: L4D-17F-DOCS
required_handoff_ids:
  - H-L4D-17ABC-CONTRACT-01-v1
  - H-L4D-17D-MEDIA-CONTRACT-01-v1
  - H-L4D-17E-CONTRACT-01-v1
  - H-L4D-17F-MB-EVIDENCE-CONTRACT-01-v1
sequence_gate_handoff_id: H-L4D-17E-MB-FIX-01-v1
artifact_byte_binding_ids: []
external_artifact_reads: []
output_handoff_id: H-L4D-17F-DOCS-FIX-01-v1
next_prompt_id: L4D-18A-SHARED
branch: release/l4tools-1.8.2-beta-1
report_path: l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-report.md
candidate_format: DETACHED_V1
candidate_path: l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16, 17, 18, 19]
```

## Contract gate до проверок

Исторический `L4D-17F-DOCS-FIX-01-report.md` имеет `BLOCKED_TESTS` и
сохраняется неизменным. Дополнение 2026-09-27 также не меняет его verdict.
Окончательный отчёт по новому `report_path` должен назвать каждую закрытую
причину исходного blocker и источник evidence; candidate публикуется только
после полного успеха по §9.

Решение владельца о release baseline: **l4tools `1.8.2-beta-1` принять в
релиз как есть**. Не требуй изменения или пересборки Agent. Для проверки
допущены существующие `test05`/tenant 10000 и terminal 1000007, согласованный
mock YooKassa и короткие периоды. Ожидание трёх лет не является техническим
критерием этого 17F; retention invariant контракта сохраняется.
Текущую версию Agent можно подтвердить адресным наблюдением оператора;
если binary hash терминала не измерен, так и запиши. Это не причина
пересобирать принятый release baseline.

Выполни §2 `PROMPT-STANDARD.md` для **четырёх прямых входов**. Сверь
единственность `ACCEPTED`, версии, producer commits, deployment
`DOCS_PUBLISHED`, отсутствие отзывов, потребителя либо точный адресный
допуск регистрации и Git/raw SHA-256 всех 13 Markdown-артефактов.
Проверь публикацию registration/prompt commit в remote до запуска.

`H-L4D-17E-MB-FIX-01-v1` — только sequence gate по §8.4. Его
MenuBuilder исходники не становятся предметным входом и не читаются.
Исходные `H-L4D-17A-TOOLS-v1`, `H-L4D-17B-PB-v1`,
`H-L4D-17C-IOT-v1`, `H-L4D-17D-MEDIA-v1` — provenance нового
data-only пакета, не дополнительные required inputs. Не обходи
рекурсивно их отчёты или исходники. `H-L4D-17D-MEDIA-CONTRACT-01-v1`
первоначально адресован 17E; его использование здесь допускается
только этой точной регистрацией по §8.

`H-L4D-17F-MB-EVIDENCE-CONTRACT-01-v1` даёт конечный data-only пакет
трёх MenuBuilder corrective reports, исторического media soak,
release baseline Agent и регистрации test05. Читай только семь
перечисленных в принятом handoff файлов и их Git/raw digest; исходные
MenuBuilder отчёты и `.agent-context` не становятся отдельным scope.

Если любой предметный артефакт недоступен или digest не совпал,
верни `BLOCKED_CONTRACT` до runtime проверки. Документальный экспорт
передаёт историческое evidence, но не доказывает текущий deploy.

## Black-box приёмка

Повтори критерии исходного `L4D-17F-DOCS.md` через разрешённые
deployed test/public/internal interfaces. Работай только в
`l4desk-service`; не открывай соседние runtime repositories, DB,
приватные env или исходники, не меняй server files. Для браузера
используй `playwright-cli`; пользователь может выполнить действия
в собственной авторизованной сессии и сообщить наблюдение.

Построй и заполни матрицу для:

1. Self-registration → email confirmation → tenant → terminal →
   provisioning/PIN → текущий Agent online. Проверяй также старого
   пользователя и владение терминалом. Не рассылай массовые письма.
2. Одна console либо video session, запрет одновременного запуска,
   session facts, stop/retry/duplicate, correlation ID, отсутствие
   зависания и HTTP 500. Для длительных периодов используй 10–30 минут,
   а 120 минут связывай с уже опубликованным историческим E2E и
   доказательством эквивалентности механики.
3. Usage/free quota → mock/sandbox payment → идемпотентные webhook/poll
   → ledger/balance → grace/block/stop; округление в пользу потребителя,
   отсутствие повторного начисления и debit=credit. Реальные списания
   и обнуление production-баланса не выполняй.
4. Hub correlation/mismatch и archive manifest/restore evidence через
   безопасный dry-run/read-only режим, без purge реальных данных.
   Для 17F достаточно проверенного producer manifest/restore dry-run и
   consumer contract evidence. Запуск production archive worker,
   backup volume и длительное ожидание относятся к последующим 18* шагам.
5. Точные deployed image/source/schema/feature flags и rollback
   для Agent, ProcessingBackend, IoT, media и MenuBuilder на момент
   проверки. Исторические значения из экспортов не подставляй как
   текущие без независимого evidence.

Используй ранее одобренные test04/tenant 1000/terminal 1000005 и
test05/tenant 10000/terminal 1000007 с изолированным mock-контуром
только после проверки их текущего состояния.
Если для обязательного сценария нет безопасного test path или
действующего доступа, зафиксируй конкретный `BLOCKED_TESTS` либо
`BLOCKED_DEPLOY` и требуемый следующий corrective scope. Не создавай
новые credentials, tenant, платежи или терминалы по умолчанию.

## Выход

Отчёт должен различать проверенное сейчас и историческое evidence;
включать input version/digest matrix, команды/HTTP statuses, UTC
время, версии, критические дефекты, пределы теста и rollback.
При defect выпусти список отдельных corrective prompt IDs по одному
scope; `ACCEPTED` 17F не добавляй.

При полном успехе опубликуй неизменяемый report R по `report_path`,
затем отдельный candidate C по `candidate_path` с
`contract_kinds: [REPORT, SEQUENCE_GATE]`, `report_commit: R`,
точным SHA-256 отчёта, `producer_commit` опубликованного 17F docs-пакета
и матрицей реально проверенных runtime commits/images в payload;
consumer — `L4D-18A-SHARED`. Контроллер независимо проверяет
candidate и только затем append-only добавляет `ACCEPTED`.
