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
- Независимый controller review подтвердил `BLOCKED_CONTRACT` и выявил
  отдельный дефект входа `H-L4D-17D-MEDIA-v1`: у принятого блока
  `contract_kinds: [REPORT, DEPLOYMENT]` отсутствуют `artifact_paths` и
  `artifact_sha256`. Это нарушает §1.4 журнала. Допуск адресации §8 и
  привязка байтов §10.4 не создают недостающие артефакты или digest.
- Исходный `L4D-17E-MB.md` задаёт `report_path` как
  `MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-report.md`, а опубликованный
  report R и candidate используют
  `MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-acceptance-2026-09-27.md`.
  У исходного prompt нет `registration_id` и `candidate_path`; §8–9 требуют
  точного совпадения этих метаданных. Report R не редактировать.
- В report R/матрице нет полной таблицы всех 11 прямых входов с точными
  `contract_version` и digest, требуемой §2.5. В candidate добавлен
  `COMMERCIAL_CONTROL`, хотя выход исходного 17E prompt задаёт только
  `REPORT/DEPLOYMENT`. Исправлять это следует в новом пакете, не в R.

## Следующее решение контроллера

Минимальный корректный порядок после явного разрешения контроллеру по §8:

1. Выпустить через адресный provider/corrective шаг новый media handoff с
   immutable report/artifact и digest. Старый `H-L4D-17D-MEDIA-v1` оставить
   неизменным; использовать его только как `sequence_gate_handoff_id`.
2. Зарегистрировать отдельный corrective prompt 17E с новым `prompt_id` и
   точным конечным списком `required_handoff_ids`: новый media handoff и
   остальные десять входов. В `authorized_inputs` указать те же ID,
   `contract_version` и `producer_commit`; явно разрешить `DETACHED_V1`.
   В prompt и регистрации должны совпасть `report_path`, `candidate_path`,
   scope, output и next. Опубликовать регистрацию до исполнения.
3. Выпустить новый immutable report R2 с таблицей версий/digest всех прямых
   входов и новый candidate C2 с `REPORT/DEPLOYMENT`, точными путями и
   хешами. Старый R (`3d1ff475`) сохранить неизменным как evidence.
4. Независимо сверить R2/C2, опубликованные байты, runtime image/flags и
   ограничения E2E; только потом append-only принять handoff и открыть 17F.

Один адресный допуск для исходных 11 входов недостаточен из-за дефекта 17D.

До этого PR #4 остаётся draft. Функциональные результаты 17E — в
`MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-acceptance-2026-09-27.md`;
они не подменяют формальный contract gate.
