# L4D-17E-MB: формальный contract gate

Срез 2026-09-27. Исходный `L4D-17E-MB` остаётся **BLOCKED_CONTRACT**;
зарегистрированный `L4D-17E-MB-FIX-01` готов к исполнению, но не принят.
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

## Media blocker: опубликованное исправление

По прямому поручению пользователя выполнен документационный export §10.1:
`H-L4D-17D-MEDIA-CONTRACT-01-v1` со статусом `ACCEPTED` опубликован в
журнале commit `31122c3a2c2b4c38436794a23e92169c608bf231`.
Два data-only файла находятся в Git commit
`77a666f17cc5cb154b6a46a8a119ed0594c087c0`; их SHA-256 проверены
по raw Git bytes и внесены в новый handoff. Исторические шесть digest
17D совпали 6/6 только после LF → CRLF; новые суммы привязаны к
опубликованным LF-байтам. Старый `H-L4D-17D-MEDIA-v1` не редактировался.

Новый handoff адресован `L4D-17E-MB-FIX-01` и пригоден как предметный media
input после регистрации этого corrective prompt. Старый 17D допустим только
как sequence gate. Export подтверждает исторический отчёт, а не новый
runtime smoke; текущее состояние media следует проверить в приёмке 17E.
Исходный 17E остаётся `BLOCKED_CONTRACT`; дальнейший путь — corrective.

## Передача 17E FIX

В `ccab98730d6fde1f2e5335c35bad63e3303f4227` опубликованы точный
prompt `L4D-17E-MB-FIX-01.md` и регистрация
`R-L4D-17E-MB-FIX-01-v1`. Remote ref подтверждён. Регистрация содержит
11 прямых входов, старый 17D только как sequence gate, `DETACHED_V1`,
точные report/candidate paths и конечный read grant. Промпт и регистрация
совпали по всем обязательным полям и спискам.

Девять `ARTIFACT_BYTE_BINDING` доказывают 112 исторических пар
LF/CRLF для входов 06C, 08B–14 и 16; read-only отчёт опубликован в
`c3cd78449e34708783ae5013bb216045ccbce4a8`. Каждая пара повторно
сверена с принятым handoff, raw Git blob и указанным artifact commit.
Для 06C FIX-отчёт найден в отдельном commit `8004b5b`, что зафиксировано
в binding. Вход 17C совпадает с Git/raw без binding.

Это готовность **contract gate к передаче исполнителю**, а не приёмка
коммерческого контура. Исполнитель должен пройти локальные и runtime
критерии исходного 17E, выпустить новый report R2 и candidate C2;
контроллер обязан проверить их независимо. Исходный report R не менять.

## Следующее решение контроллера

Минимальный корректный порядок после явного разрешения контроллеру по §8:

1. Media handoff выполнен: использовать
   `H-L4D-17D-MEDIA-CONTRACT-01-v1` как предметный input; старый
   `H-L4D-17D-MEDIA-v1` — только `sequence_gate_handoff_id`.
2. Регистрация и prompt corrective 17E выполнены в `ccab987`; передать
   исполнителю этот commit и путь к `L4D-17E-MB-FIX-01.md`.
3. Выпустить новый immutable report R2 с таблицей версий/digest всех прямых
   входов и новый candidate C2 с `REPORT/DEPLOYMENT`, точными путями и
   хешами. Старый R (`3d1ff475`) сохранить неизменным как evidence.
4. Независимо сверить R2/C2, опубликованные байты, runtime image/flags и
   ограничения E2E; только потом append-only принять handoff и открыть 17F.

Один адресный допуск для исходных 11 входов недостаточен из-за дефекта 17D.

До этого PR #4 остаётся draft. Функциональные результаты 17E — в
`MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-acceptance-2026-09-27.md`;
они не подменяют формальный contract gate.
