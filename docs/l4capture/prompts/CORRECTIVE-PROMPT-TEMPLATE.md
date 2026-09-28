# Шаблон корректирующего промпта (Corrective Prompt) каскада L4Capture

Используется только после блокировки (`BLOCKED_*`) или отклонения (`REJECTED`) отчёта основного шага. Контроллер заполняет шаблон конкретными данными и регистрирует corrective-запись в журнале до запуска исполнителя.

```yaml
prompt_id: <L4C-XX-SCOPE-FIX-01>
scope_project: <tools/l4capture | tools/l4desk>
scope_root: <D:\repo\platerra\Public\etranprocessing\tools\l4capture>
blocked_prompt_id: <L4C-XX-SCOPE>
required_handoff_ids:
  - <предшествующий принятый H-L4C-YY-v1>
sequence_gate_handoff_id: <последний ACCEPTED основной шаг перед заблокированным>
output_handoff_id: <H-L4C-XX-SCOPE-FIX-01-v1>
next_prompt_id: <повтор исходного шага L4C-XX-SCOPE>
branch: l4capture/<prompt-id-lowercase>
report_path: docs/l4capture/handoffs/<prompt_id>-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4capture/handoffs/<prompt_id>-candidate.md
registration_id: <R-L4C-XX-SCOPE-FIX-01-v1>
```

---

## Обязательные инструкции агенту

1. Прочитай `PROMPT-STANDARD.md` и проверь contract gate по `contract-handoff.md` в `docs/l4capture/prompts/`.
2. Работай строго внутри разрешённого `scope_root` (`tools\l4capture`).
3. Запрещено модифицировать или использовать файлы в `l4desk-service`.
4. Воспроизведи зафиксированный дефект падающим тестом.
5. Выполни минимальное исправление кода, не нарушающее архитектурные контракты `l4capture_arch_final.md`.
6. Выполни полную сборку (x86/x64, `/MT`) и все тесты.
7. Подготовь отчёт `docs/l4capture/handoffs/<prompt_id>-report.md` и отдельный кандидат `docs/l4capture/handoffs/<prompt_id>-candidate.md` в формате `DETACHED_V1`.
8. Передай результат контроллеру для повторной приёмки.
