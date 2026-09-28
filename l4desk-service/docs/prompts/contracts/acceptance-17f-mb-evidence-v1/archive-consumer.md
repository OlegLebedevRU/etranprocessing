# L4D-17F-MB-ARCHIVE-CONSUMER-EVIDENCE-01 — приём манифеста l4media

Статус: `CONTRACT_TEST_PASS`; общий 17F остаётся `BLOCKED_TESTS`.
Проверено 2026-09-27 17:16 UTC локально, без записи в рабочую БД и без
изменения серверных файлов. Владелец импорта и Hub — MenuBuilder; producer
манифеста — l4media. Входом послужил неизменённый синтетический
`arch-media-2026-09-17fprobe` из
`l4desk-service/docs/handoffs/evidence/17f-archive-20260927/manifest.json`.

## Проверка

В `tests/test_archive_manifests.py` добавлен контрактный тест, который
передаёт **реальный output архиватора** в `ArchiveService.import_manifest`
с виртуальной датой `2027-01-01`. Результат: `verified`, 6 записей,
`vol://2026/09/l4media/arch-media-2026-09-17fprobe`. Повторный импорт
сохранил одну запись; изменение контрольной суммы для того же batch ID
вызвало `ArchiveConflictError`. `get_archive_batch` для обычного
потребителя вернул состояние без физического пути тома и без raw manifest.

| Проверка | Итог |
|---|---|
| Адресный тест consumer | 1 passed |
| Все тесты MenuBuilder backend | 525 passed, 51 warnings |
| `uv run ruff check --fix app tests` | pass |
| `uv run ruff format app tests` | pass; форматирован один изменённый файл |
| `uv run pyright app tests/test_archive_manifests.py` | 0 errors, 0 warnings |

Работающий production image содержит те же байты `archive_service.py`,
`archive_schemas.py` и `hub_service.py`, что локальный источник (SHA-256
сверен по каждому файлу). Это подтверждает совпадение логики в трёх
проверенных файлах, но не является runtime-тестом HTTP route или БД.

## Граница результата

Тест использует in-memory DB double и `check_volume_availability=False`:
он проверяет совместимость producer→consumer, идемпотентность и выдачу
данных Hub, но не реальный PostgreSQL, файловый том и HTTP-авторизацию.
В рабочую БД фиктивный архив не импортировался; production Hub по-прежнему
не имеет архивной записи для этого fixture. Проверку реального архива и
backup-монта нельзя объявлять пройденной. Трёхлетнее ожидание не входит
в технические критерии 17F по решению пользователя; контрактное поле
retention сохранено без изменения.

Следующий отдельный gate — текущая source/image/schema/flags/rollback
матрица и адресная корреляция Hub на существующих финансовых фактах.
