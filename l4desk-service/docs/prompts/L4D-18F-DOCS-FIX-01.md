# L4D-18F-DOCS-FIX-01 — итоговый реестр ограниченного выпуска

```yaml
prompt_id: L4D-18F-DOCS-FIX-01
scope_project: l4desk-service
scope_root: D:\repo\platerra\Public\etranprocessing-l4d-18f-docs\l4desk-service
prompt_type: corrective-release-governance
registration_id: R-L4D-18F-DOCS-FIX-01-v1
blocked_prompt_id: L4D-18F-DOCS
required_handoff_ids: [H-L4D-18A-SHARED-v1, H-L4D-18B-PB-v1, H-L4D-18C-IOT-v1, H-L4D-18D-MEDIA-v1, H-L4D-18E-MB-v1]
sequence_gate_handoff_id: H-L4D-18E-MB-v1
artifact_byte_binding_ids: []
external_artifact_reads:
  - handoff_id: H-L4D-18A-SHARED-v1
    artifact_commit: 2b38855000d67f06269ddf40e6305382abe2d3a9
    paths:
      - shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-report.md
      - shared/docs/l4desk/package-source-v011.json
  - handoff_id: H-L4D-18B-PB-v1
    artifact_commit: 3d76c7d6d5165959e4cd077b4730a5712d7e1f6a
    paths:
      - ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-report.md
  - handoff_id: H-L4D-18C-IOT-v1
    artifact_commit: fa7a91a631ced5134104e8bef61a69aa620c26a3
    paths:
      - docs/l4desk/handoffs/L4D-18C-IOT-FIX-01-report.md
      - docs/l4desk/handoffs/evidence/18c-rollout/18c-production-image-final.json
      - docs/l4desk/handoffs/evidence/18c-rollout/18c-production-archive.json
  - handoff_id: H-L4D-18D-MEDIA-v1
    artifact_commit: ed453db73b2dad4855cb099dfa5fdf9cf94ff655
    paths:
      - l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-report.md
      - l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-evidence.json
  - handoff_id: H-L4D-18E-MB-v1
    artifact_commit: c590c45476913935276a7639673380e0a53e29fc
    paths:
      - MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-report.md
      - MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-inputs.json
output_handoff_id: H-L4D-18F-DOCS-v1
next_prompt_id: NONE
branch: l4desk/l4d-18f-docs
report_path: l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-candidate.md
architecture_sections: [1, 3, 4, 5, 14, 15, 16, 17, 18, 19]
```

## Gate

Применить `PROMPT-STANDARD.md` и точную регистрацию v1. Пять входов должны
иметь по одному `ACCEPTED` в журнале, совпадающие версии/commits и digest
каждого разрешённого artifact. Старые `consumers` не исправлять: регистрация
дополняет только адресацию. 18E одновременно является sequence gate. Если
артефакт внешнего IoT-репозитория недоступен по указанному commit или его
Git/raw SHA-256 расходится с принятым handoff, вернуть `BLOCKED_CONTRACT`;
не заменять его похожим файлом текущего репозитория.

## Результат

Работать только в `l4desk-service`. Прочитать лишь перечисленные data-only
отчёты и evidence; не открывать исходники соседних проектов, приватные env,
ключи или базы данных. Сверить фактически работающие версии через штатные
read-only операционные интерфейсы. Различать принятый 18E baseline и
последующие изменения: Alembic 028, post-18E MenuBuilder, его registry digest
и текущий frontend `dist`. Исторический report не доказывает текущий runtime.

Браузерные изменения после 18E учесть по
[`L4D-18F-VIDEO-UX-DELTA.md`](L4D-18F-VIDEO-UX-DELTA.md): source commits
`239345b` и `23cc4cb` требуют отдельной сверки раздаваемого `dist` и
browser smoke для Medium/HD, размера кадра и локальной паузы F8. Этот индекс
не расширяет `external_artifact_reads` регистрации и не превращает новый
код в исторически принятый 18E. Если изменения входят в целевой выпуск,
отсутствие runtime/browser evidence является открытым `PENDING`/`DRIFT`.

Выпустить единый реестр: Git commit → image/package digest → deployed service,
schema/API/Agent versions, feature flags и границы активации; матрицу
совместимости; владельцев, endpoints без секретов и порядок rollback. Привести
проверенные команды/результаты мониторинга регистрации, PIN, online,
активных сессий, задержки usage/event, ledger/reconciliation, YooKassa,
grace/block и архивного checksum. Зафиксировать проверенную процедуру
backup/restore и фактическую hot-retention. Трёхлетний срок описывать только
как процедурную политику: ждать три года и заявлять техническую проверку срока
для принятия нельзя.

Закрытие ограниченного выпуска возможно только при зелёном текущем smoke и
совпадении deployed versions с реестром. Общую коммерческую активацию не
объявлять принятой: пять исторических source-hash gaps tenant 1000 остаются
отдельным блокером. Не включать выключенные workers/flags и не выполнять
платёж, миграцию или deploy в рамках этого docs-шага. Отсутствующее evidence
и несовпадающие версии фиксировать как blocker, не сглаживать формулировкой.

При успехе опубликовать окончательный report R, затем отдельный candidate C
по §9 стандарта с `contract_kinds: [REPORT, SEQUENCE_GATE]`,
`next_prompt_id: NONE`, `activation_scope: restricted` и фактическими
version/digest matrix. Лишь независимый контроллер может добавить принятый
handoff и `CLOSED_ACCEPTED` в append-only журнал.
