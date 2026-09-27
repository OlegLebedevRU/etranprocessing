# L4D-17E-MB: формальный contract gate

Срез 2026-09-27. Статус: **BLOCKED_CONTRACT для принятия handoff**.
Это аудит документов и Git; runtime и инфраструктура не изменялись.

## Проверено

- В `L4D-17E-MB.md` перечислены 11 required handoff. Каждый встречается
  в `contract-handoff.md` ровно один раз со статусом `ACCEPTED`.
- Три входа адресованы 17E: `H-L4D-17D-MEDIA-v1`,
  `H-L4D-17C-VIDEO-WATCH-MB-v1`, `H-L4D-06C-MB-v1` (через `ALL_FOLLOWING`).
- Восемь входов ниже не имеют `L4D-17E-MB`/`ALL_FOLLOWING` в `consumers`:

| Handoff | Текущий consumer |
|---|---|
| `H-L4D-08B-MB-v1` | `L4D-09-MB` |
| `H-L4D-09-MB-v1` | `L4D-10-MB` |
| `H-L4D-10-MB-v1` | `L4D-11-MB` |
| `H-L4D-11-MB-v1` | `L4D-12-MB` |
| `H-L4D-12-MB-v1` | `L4D-13-MB` |
| `H-L4D-13-MB-v1` | `L4D-14-MB` |
| `H-L4D-14-MB-v1` | `L4D-15A-DOCS` |
| `H-L4D-16-MB-v1` | `L4D-17A-TOOLS` |

- Точного адресного допуска для `prompt_id: L4D-17E-MB` в журнале нет.
  Регистрация `R-L4D-17C-VIDEO-WATCH-MB-v1` относится к другому prompt
  и не наследуется 17E.
- `PROMPT-STANDARD.md` §9 допускает `DETACHED_V1` только при отдельной
  регистрации с `detached_candidate_approved: true`; её для 17E нет.
  В опубликованном candidate это поле равно `false`.
- Все 12 `artifact_paths` candidate совпадают с Git/raw SHA-256 на
  `report_commit: 3d1ff47586c52fd367d4bba4495439aeb4571949`.
  Исправлен `producer_commit`: он указывает на реализацию `9bda9ce`,
  тогда как `report_commit` указывает на неизменённый отчёт. Candidate
  остаётся evidence, а не принятым контрактом.

## Следующее решение контроллера

Контроллеру нужно независимо решить адресацию восьми исторических
контрактов и допустимость `DETACHED_V1` для 17E, не редактируя принятые
handoff задним числом. Если будет оформлен точный адресный допуск, его
`authorized_inputs` по §8 должен совпасть со всеми 11 required ID,
версиями и `producer_commit` из принятых блоков. После публикации допуска
нужна новая независимая сверка report/candidate, deploy flags, digest и
остаточных E2E. Только затем контроллер может добавить `ACCEPTED` в журнал
и открыть `L4D-17F-DOCS`.

До этого PR #4 остаётся draft. Функциональные результаты 17E — в
`MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-acceptance-2026-09-27.md`;
они не подменяют формальный contract gate.
