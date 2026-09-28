# L4D-17E-MB-FIX-01 — Повторная приёмка коммерческого контура

```yaml
prompt_id: L4D-17E-MB-FIX-01
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing-l4tools-182\MenuBuilder
prompt_type: corrective-acceptance
registration_id: R-L4D-17E-MB-FIX-01-v2
blocked_prompt_id: L4D-17E-MB
required_handoff_ids:
  - H-L4D-17D-MEDIA-CONTRACT-01-v1
  - H-L4D-17C-VIDEO-WATCH-MB-v1
  - H-L4D-06C-MB-v1
  - H-L4D-08B-MB-v1
  - H-L4D-09-MB-v1
  - H-L4D-10-MB-v1
  - H-L4D-11-MB-v1
  - H-L4D-12-MB-v1
  - H-L4D-13-MB-v1
  - H-L4D-14-MB-v1
  - H-L4D-16-MB-v1
sequence_gate_handoff_id: H-L4D-17D-MEDIA-v1
artifact_byte_binding_ids:
  - B-L4D-06C-MB-GIT-v1
  - B-L4D-08B-MB-GIT-v1
  - B-L4D-09-MB-GIT-v1
  - B-L4D-10-MB-GIT-v1
  - B-L4D-11-MB-GIT-v1
  - B-L4D-12-MB-GIT-v1
  - B-L4D-13-MB-GIT-v1
  - B-L4D-14-MB-GIT-v1
  - B-L4D-16-MB-GIT-v1
external_artifact_reads:
  - handoff_id: H-L4D-17D-MEDIA-CONTRACT-01-v1
    artifact_commit: 77a666f17cc5cb154b6a46a8a119ed0594c087c0
    paths:
      - l4desk-service/docs/prompts/contracts/media-17d-v1/contract.md
      - l4desk-service/docs/prompts/contracts/media-17d-v1/verification.md
  - handoff_id: H-L4D-13-MB-v1
    artifact_commit: 4a6e124d870e06a2001a83464af2beb0123639b5
    paths:
      - nginx-configs/port_3000.conf
output_handoff_id: H-L4D-17E-MB-FIX-01-v1
next_prompt_id: L4D-17F-DOCS
branch: release/l4tools-1.8.2-beta-1
report_path: MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v2-report.md
candidate_format: DETACHED_V1
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v2-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 6, 7, 9, 10, 13, 14, 15, 16, 17]
```

## Contract gate до реализации

Выполни §2 `PROMPT-STANDARD.md` для всех 11 прямых входов. Для
`H-L4D-17D-MEDIA-v1` проверь только sequence gate по §8.4: он не является
предметным входом, его отсутствующие digest нельзя восполнять по догадке.
Предметный media-вход — `H-L4D-17D-MEDIA-CONTRACT-01-v1`, документальный
экспорт исторического 17D evidence. Он не заменяет проверку актуального
runtime media на этапе acceptance.

Для девяти исторических входов используй только перечисленные
`ARTIFACT_BYTE_BINDING` по §10.4. Сверяй сырые Git bytes по
`git_blob_sha256` и отдельно исходные historical SHA-256 с принятым
handoff; не нормализуй текст для принятия. Если blob недоступен по точному
`artifact_commit`, binding отозван или причина не `LF_CRLF_ONLY`, верни
`BLOCKED_CONTRACT`. В `H-L4D-06C-MB-v1` FIX-отчёт опубликован отдельно в
`8004b5b575a72a70e33a7ded11346c8fb4df0e02`; используй commit,
указанный в binding для этого артефакта.

Read grant разрешает только чтение и SHA-256 двух media Markdown-файлов и
декларативного `nginx-configs/port_3000.conf`, перечисленных выше. Последний
лежит вне `MenuBuilder`, но входит в `artifact_paths` принятого 13-го
handoff. Его SHA-256 привязан к отдельному `report_commit` 13-го handoff;
первый допуск v1 был отозван после несовпадения на `producer_commit`.
Не исполняй конфигурацию, не открывай другие файлы соседних
проектов и не обходи ссылки внутри документов. Все остальные артефакты
проверяй внутри scope `MenuBuilder`.

В рабочем отчёте до проверок выпиши для каждого прямого входа точные
`handoff_id`, `contract_version`, `producer_commit`, `artifact_paths` и
historical/Git SHA-256 либо raw match. Сверь `compatibility`, deployed
status, отзыв/замену и фактическую доступность каждого артефакта.

## Приёмка

Повтори критерии исходного `L4D-17E-MB.md` только внутри MenuBuilder:
backend/frontend suite, lint/type/build и consumer fixtures; регистрация,
терминал, console/video, бесплатная квота и платное продолжение, периоды,
платёжная идемпотентность, ledger/reconciliation, Hub и archive. Для
зависящих от времени сценариев используй короткие 10–30-минутные периоды,
как указал пользователь; исходные 120-минутные требования доказывай
имеющимся историческим E2E и отдельной проверкой эквивалентности механики.
Не выдавай локальный тест за runtime E2E. При недостатке обязательного
evidence верни соответствующий `BLOCKED_*` и конкретный следующий шаг.

Сверь используемые source commit, image, schema, feature flags, откат,
нулевой ledger imbalance и reconciliation mismatch. Production commercial
flags остаются выключенными; тестовый контур ограничен tenant 1000 и
600 секундами. Новые реальные платежи/массовые письма не инициировать.
Используй уже опубликованный отчёт
`L4D-17E-MB-acceptance-2026-09-27.md` как историческое evidence, не меняя
его байты и не принимая его статус автоматически.

## Выход и handoff

Опубликуй новый неизменяемый отчёт по `report_path` с полной таблицей
версий и digest всех 11 прямых входов, результатами проверок и пределами
runtime evidence. Затем по §9 опубликуй отдельный candidate по
`candidate_path` с `contract_kinds: [REPORT, DEPLOYMENT]`, точным SHA-256
отчёта, `report_commit`, настоящим `producer_commit` реализации и
consumer `L4D-17F-DOCS`. Candidate не включай в собственный список
артефактов. Контроллер принимает его отдельно; сам candidate не означает
`ACCEPTED`.

После приёмки FIX контроллер должен привести входной prompt 17F к
принятому corrective output и документальному media input. Исходный 17F
ссылается на `H-L4D-17E-MB-v1` и дефектный `H-L4D-17D-MEDIA-v1`, поэтому
его нельзя запускать без отдельной адресной регистрации или исправления.
