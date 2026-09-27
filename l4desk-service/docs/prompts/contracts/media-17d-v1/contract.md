# L4D-17D media: самостоятельный пакет доказательств

Версия: `1.0.0`. Тип: `REPORT/DEPLOYMENT`. Это документальный экспорт
принятого 17D media evidence для корректирующего consumer 17E. Он не меняет
медиапротокол и не подтверждает повторный runtime smoke.

## Предмет

Отчёт `L4D-17D-MEDIA-report.md` в Git commit
`85cd6440d115bdf8a6f5cb65821dd57a68455388` фиксирует:

- приёмку start → health → stop → reconcile тестовой медиасессии без
  оставшегося маршрута или mountpoint;
- проверки локального архива: детерминированность, dry-run, restore sample,
  блокировку purge при ошибке и ограничения активных потоков;
- исторический результат 21/21 тестов, Ruff/Pyright и production smoke;
- отсутствие удаления реальных архивных данных в той проверке.

Это **исторические утверждения отчёта 17D**, а не результат новых запусков.
Consumer обязан отдельно сверить работающую версию media, флаги и доступный
runtime evidence на момент собственной приёмки.

## Неподвижные источники

Все пути ниже относятся к дереву Git
`85cd6440d115bdf8a6f5cb65821dd57a68455388`. SHA-256 вычислен по
сырым `git show <commit>:<path>` байтам. В скобках — исходный commit
реализации, где соответствующий blob идентичен байтам из `85cd644`.

| Путь | Git/raw SHA-256 | Источник |
|---|---|---|
| `l4media/ingress/openapi.json` | `831acbc9753bab5d88c036adf9f43d98a28ca45f2143b06d12cb0ce758f80ce4` | `37adfd01e5492e6b61e8ecb243579389ae2d858e` |
| `l4media/archive/pipeline.py` | `aff08a1efa28b564b055f2a0dbfe88c09814a2d0b0647cc9fc35e080fd05410e` | `fe9dcd1b42e9d0b811e903031226e3903501ebe9` |
| `l4media/archive/canonical.py` | `880002194529eb1659983e44e4f0e5a2424695dad4727d9a388f475985a429a0` | `fe9dcd1b42e9d0b811e903031226e3903501ebe9` |
| `l4media/archive/guards.py` | `c8b74c12c4043b1b791db292c1def0bf62ea7a0924f66cc9b4ba82fbe6e951d7` | `fe9dcd1b42e9d0b811e903031226e3903501ebe9` |
| `l4media/tests/test_archive_pipeline.py` | `124c108a6a0fe796ea00bdf083755879584cb339ce9d9923436ecc720a8cab35` | `fe9dcd1b42e9d0b811e903031226e3903501ebe9` |
| `l4media/tests/test_archive_restore.py` | `9fe6dfd90965fed2349c5791ce5c0f2a33be2354bf1deec11a3233dcc55defbd` | `fe9dcd1b42e9d0b811e903031226e3903501ebe9` |
| `l4media/docs/l4desk/handoffs/L4D-17D-MEDIA-report.md` | `3112ee34919e8ef6ceeb4d6d935e2af275dfc07f23012a2afc055192a9f700c0` | `85cd6440d115bdf8a6f5cb65821dd57a68455388` |

Старые шесть SHA-256 в отчёте 17D вычислены по CRLF-копиям тех же файлов.
Все 6/6 совпадают при единственном преобразовании LF → CRLF. В этом новом
пакете используются исходные Git bytes; старые значения не подменяются и
не превращаются в новый byte binding для старых handoff.

## Граница потребления

`H-L4D-17D-MEDIA-v1` остаётся принятым историческим шагом, но его блок в
журнале не содержит `artifact_paths`/`artifact_sha256`. В корректирующем
17E его следует проверять только как `sequence_gate_handoff_id`, отдельно
от предметного входа этого пакета. Потреблять этот пакет можно только после
добавления нового `ACCEPTED` handoff с digest опубликованных файлов данного
каталога и точной адресацией corrective prompt.
