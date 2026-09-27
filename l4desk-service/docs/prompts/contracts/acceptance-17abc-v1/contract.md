# L4D-17A–17C: документальный экспорт принятых handoff

Версия `1.0.0`. Тип `REPORT/DEPLOYMENT`. Это самостоятельная data-only
копия трёх **принятых** записей журнала для корректирующего входа 17F.
Источником являются только сырые Git-байты `l4desk-service/docs/prompts/contract-handoff.md` в
`2ad0c8b868a092143f3e36a3719238b3a31aa17f`; исходники и отчёты проектов при этом экспорте не читались.
Прежние handoff остаются неизменными и не становятся предметными входами 17F.

## Историческое evidence, записанное контроллером

- 17A: Agent artifact `l4tools 1.7.7`, SHA-256
  `874f5444d2d4cc9bdee39e7c25a1265a2dd98f388aca49ef205ffb764531cd88`;
  golden fixtures 25/25, backward compatibility с 1.7.6. Это утверждения
  принятого журнала, текущая установка агента не проверялась.
- 17B: Alembic head `027`, API version `0.1.0`, исторический image
  `user1-processing-backend` (`e2194a9b3341`), pytest 130/130,
  PIN fixtures 17/17, production-safe smoke. Детали хранятся в принятом
  журнале; текущий image и API должны быть проверены отдельно.
- 17C: IoT app1 `0.1.1`, web concurrency `1`; исторические suite
  422 passed, targeted fixtures 77 passed, archive dry-run/restore/cursor,
  session lock и watch snapshot smoke. Текущая версия app1 и совместимость
  с новым Agent требуют отдельной black-box проверки.

## Канонические снимки принятых записей

### 17A Agent

- Accepted handoff: `H-L4D-17A-TOOLS-v1`; producer commit: `9ed5b2221c1cd6c6286ff4c7fbe0f1c527b53141`; artifact version: `1.7.7`; environment: `artifact-registry`.
- Complete accepted journal block, including HTML markers: Git/raw SHA-256 `3be6e23993f02f5a73bc7bed4dfe4358d9c7f93745f662ad1d4243b34e058f6d` (683 bytes).

```yaml
handoff_id: H-L4D-17A-TOOLS-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - FIXTURES
producer_prompt_id: L4D-17A-TOOLS
producer_scope_project: tools
producer_report_path: tools/docs/l4desk/handoffs/L4D-17A-TOOLS-report.md
producer_branch: l4desk/l4d-17a-tools
producer_commit: 9ed5b22
accepted_at_utc: '2026-09-25T21:42:00Z'
contract_version: 1.0.0
artifact_version: 1.7.7
compatibility:
  backward_compatible_with:
    - 1.7.6
    - 1.7.7
  breaking_changes: false
deployment_status: ACCEPTED
deployed_environment: artifact-registry
consumers:
  - L4D-17B-PB
next_prompt_id: L4D-17B-PB
```
### 17B ProcessingBackend

- Accepted handoff: `H-L4D-17B-PB-v1`; producer commit: `bc4ec6c3ece48d75c08ff98773af7488da4ace84`; artifact version: `0.1.0 / schema 027`; environment: `production`.
- Complete accepted journal block, including HTML markers: Git/raw SHA-256 `c2c6ccccf7161bf2744672b904cbc4a7475c6ec473246c14e418f7c6a7488f01` (729 bytes).

```yaml
handoff_id: H-L4D-17B-PB-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - DEPLOYMENT
producer_prompt_id: L4D-17B-PB
producer_scope_project: ProcessingBackend
producer_report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-17B-PB-report.md
producer_branch: l4desk/l4d-17b-pb
producer_commit: bc4ec6c
accepted_at_utc: '2026-09-25T21:44:00Z'
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.0
compatibility:
  backward_compatible_with:
    - H-L4D-06A-PB-v1
    - H-L4D-04B-PB-v1
  breaking_changes: false
deployment_status: ACCEPTED
deployed_environment: production
consumers:
  - L4D-17C-IOT
next_prompt_id: L4D-17C-IOT
```
### 17C IoT app1

- Accepted handoff: `H-L4D-17C-IOT-v1`; producer commit: `7c6f75f (accepted short SHA; external repository)`; artifact version: `0.1.1`; environment: `production`.
- Complete accepted journal block, including HTML markers: Git/raw SHA-256 `0b7addc5428f859321661b1f4c738ce7bbde813f5679bed752771f0025b63a55` (797 bytes).

```yaml
handoff_id: H-L4D-17C-IOT-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - DEPLOYMENT
producer_prompt_id: L4D-17C-IOT
producer_scope_project: iot-rpc-rest-app
producer_report_path: docs/l4desk/handoffs/L4D-17C-IOT-report.md
producer_branch: l4desk/l4d-17c-iot
producer_commit: 7c6f75f
accepted_at_utc: '2026-09-25T21:46:00Z'
contract_version: 1.0.0
artifact_version: 0.1.1
compatibility:
  backward_compatible_with:
    - H-L4D-17C-VIDEO-WATCH-IOT-01-v1
    - H-L4D-07-IOT-v1
    - H-L4D-15B-IOT-v1
    - H-L4D-06B-IOT-v1
  breaking_changes: false
deployment_status: ACCEPTED
deployed_environment: production
feature_flags:
  web_concurrency: "1"
consumers:
  - L4D-17D-MEDIA
next_prompt_id: L4D-17D-MEDIA
```

## Граница потребления

Исходные три handoff не содержат `artifact_paths`/`artifact_sha256`;
их нельзя принимать как предметные входы 17F через адресное исключение.
Новый экспорт фиксирует **только** приведённые выше журнальные данные и
исторические утверждения контроллера. Он не доказывает байты Agent,
ProcessingBackend или IoT исходников, не содержит OpenAPI/schema fixtures
и не повторяет runtime smoke. Корректирующий 17F обязан проверить
работающие интерфейсы и версии самостоятельно; отсутствие обязательного
evidence возвращает `BLOCKED_*`.
