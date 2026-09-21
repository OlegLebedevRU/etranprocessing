# Журнал передачи контрактов каскада L4Capture (Contract Handoff Journal)

**Версия журнала:** `1.0.0`  
**Статус каскада:** `READY_FOR_L4C_01`  
**Дата создания:** 2026-09-19  
**Целевой каталог журнала и документации:** `docs\l4capture`  
**Нормативная основа:** [l4capture_arch_final.md](../l4capture_arch_final.md) и [PROMPT-STANDARD.md](PROMPT-STANDARD.md).  
**Регламент контроллера:** [HANDOFF-CONTROLLER-PROMPT.md](HANDOFF-CONTROLLER-PROMPT.md).

---

## 1. Назначение и непереговорные правила журнала

1. **Единственный источник истины:** Этот файл является единственным нормативным журналом межагентной фиксации и передачи принятых контрактов для проекта `l4capture`. В каскаде L4C не используются другие журналы.
2. **Строгий запрет `l4desk-service`:** Запрещено размещать журнал, промпты, контракты, отчёты или другие артефакты проекта `l4capture` в `l4desk-service`. Проект `l4capture` изолирован.
3. **Исключительное право записи (Append-Only):**
   - Добавление записей разрешено ТОЛЬКО Агенту-Контроллеру каскада (`HANDOFF-CONTROLLER-PROMPT.md`).
   - Исполнители-агенты НЕ редактируют этот файл напрямую; они формируют отчёты в `docs/l4capture/handoffs/<PROMPT_ID>-report.md` и кандидаты в `docs/l4capture/handoffs/<PROMPT_ID>-candidate.md`.
   - Журнал ведётся строго методом append-only: существующие принятые блоки неизменяемы (immutable).
4. **Проверка реальных байтов (SHA-256):**
   - Контроллер проверяет контрольные суммы SHA-256 реальных байтов артефактов и отчёта перед внесением записи.
   - Любое расхождение контрольной суммы блокирует приёмку (`HASH_MISMATCH`).
5. **Sequence Gate:**
   - Каждый следующий шаг требует наличия в журнале предшествующего принятого handoff-блока (`H-L4C-XX-v1`) со статусом `ACCEPTED`.
   - Пропуск шагов или параллельное продвижение запрещены.
6. **Шлюз Milestone M-1 (L4C-06):**
   - Шаг `L4C-06-MILESTONE-LIVE-VERIFY` требует подтверждения прохождения fault-матрицы (§12 архитектуры) **И** личного вердикта владельца «НОРМ». Без явного согласия владельца фиксация `H-L4C-06-v1` запрещена, а шаги фазы 2 (L4C-07 .. L4C-11) не могут быть начаты.

---

## 2. Реестр последовательности шагов каскада (11 шагов / 4 фазы)

```text
Фаза 1: Минимальный сквозной тракт и M-1
  1. L4C-01-FRAMEWORK        -> tools/l4capture
  2. L4C-02-GDI-CAPTURE       -> tools/l4capture
  3. L4C-03-OPENH264-CODEC   -> tools/l4capture
  4. L4C-04-RTP-SENDER       -> tools/l4capture
  5. L4C-05-AGENT-ADAPTER    -> tools/l4desk + tools/l4capture
  6. L4C-06-MILESTONE-LIVE-VERIFY -> tools/l4capture + tools/l4desk (M-1: СТОП до «НОРМ»)

Фаза 2: Аппаратные возможности и адаптивное качество
  7. L4C-07-DXGI-CAPTURE     -> tools/l4capture
  8. L4C-08-MF-ENCODER       -> tools/l4capture
  9. L4C-09-PROFILES-DEGRADE -> tools/l4capture

Фаза 3: Платформенная совместимость и телеметрия
 10. L4C-10-WIN7-TELEMETRY   -> tools/l4capture

Фаза 4: Упаковка и релизный пакет
 11. L4C-11-RELEASE-PACKAGE  -> tools/l4capture
```

---

## 3. Таблица зарегистрированных шагов каскада

| № | Prompt ID | Scope проекта | Входной Handoff | Выходной Handoff | Статус |
|---|---|---|---|---|---|
| 1 | `L4C-01-FRAMEWORK` | `tools/l4capture` | `READY_FOR_L4C_01` | `H-L4C-01-v1` | Ожидает запуска |
| 2 | `L4C-02-GDI-CAPTURE` | `tools/l4capture` | `H-L4C-01-v1` | `H-L4C-02-v1` | Заблокирован sequence gate |
| 3 | `L4C-03-OPENH264-CODEC` | `tools/l4capture` | `H-L4C-02-v1` | `H-L4C-03-v1` | Заблокирован sequence gate |
| 4 | `L4C-04-RTP-SENDER` | `tools/l4capture` | `H-L4C-03-v1` | `H-L4C-04-v1` | Заблокирован sequence gate |
| 5 | `L4C-05-AGENT-ADAPTER` | `tools/l4desk` + `tools/l4capture` | `H-L4C-04-v1` | `H-L4C-05-v1` | Заблокирован sequence gate |
| 6 | `L4C-06-MILESTONE-LIVE-VERIFY` | `tools/l4capture` + `tools/l4desk` | `H-L4C-05-v1` | `H-L4C-06-v1` | Заблокирован sequence gate (M-1 Шлюз) |
| 7 | `L4C-07-DXGI-CAPTURE` | `tools/l4capture` | `H-L4C-06-v1` | `H-L4C-07-v1` | Заблокирован sequence gate |
| 8 | `L4C-08-MF-ENCODER` | `tools/l4capture` | `H-L4C-07-v1` | `H-L4C-08-v1` | Заблокирован sequence gate |
| 9 | `L4C-09-PROFILES-DEGRADE` | `tools/l4capture` | `H-L4C-08-v1` | `H-L4C-09-v1` | Заблокирован sequence gate |
| 10 | `L4C-10-WIN7-TELEMETRY` | `tools/l4capture` | `H-L4C-09-v1` | `H-L4C-10-v1` | Заблокирован sequence gate |
| 11 | `L4C-11-RELEASE-PACKAGE` | `tools/l4capture` | `H-L4C-10-v1` | `H-L4C-11-v1` | Заблокирован sequence gate |

---

## 4. Зарегистрированные корректирующие шаги (Corrective Registrations)

*Раздел для записей `CORRECTIVE_REGISTRATION` при блокировках основных шагов.*

*(Записей нет)*

---

## 5. Принятые handoff-блоки (Accepted Handoff Blocks)

*Раздел для append-only добавления принятых handoff-блоков контроллером каскада.*

*(Принятых блоков пока нет — каскад готов к приёму первого шага L4C-01-FRAMEWORK)*
